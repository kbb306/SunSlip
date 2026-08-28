/*
 * sunslip.c - experimental RFC 1055 SLIP provider for Solaris 8/SPARC.
 *
 * Architecture derived from the AMIX SVR4 STREAMS SLIP driver:
 *   - a DLPI style-1 pseudo driver facing Solaris IP
 *   - a pushable STREAMS module facing a serial tty
 * The two halves share one interface state (unit 0 in this prototype).
 *
 * First real prototype: plain SLIP only, no Van Jacobson compression.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/modctl.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/stat.h>
#include <sys/cmn_err.h>
#include <sys/open.h>
#include <sys/cred.h>
#include <sys/sysmacros.h>
#include <sys/dlpi.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#define SUNSLIP_NAME       "sunslip"
#define SUNSLIP_MTU        1006
#define SUNSLIP_HIWAT      4096
#define SUNSLIP_LOWAT      1024

#define SL_END             0xc0
#define SL_ESC             0xdb
#define SL_ESC_END         0xdc
#define SL_ESC_ESC         0xdd

typedef struct sunslip_state {
    queue_t *dlpi_rq;
    queue_t *tty_rq;
    t_uscalar_t dl_state;
    t_uscalar_t sap;
    mblk_t *rx_mp;
    int escaped;
    int rx_overflow;
    unsigned long ipackets;
    unsigned long ierrors;
    unsigned long opackets;
    unsigned long oerrors;
} sunslip_state_t;

static dev_info_t *sunslip_dip;
static sunslip_state_t sunslip0;

static int sunslip_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int sunslip_attach(dev_info_t *, ddi_attach_cmd_t);
static int sunslip_detach(dev_info_t *, ddi_detach_cmd_t);
static int sunslip_dlopen(queue_t *, dev_t *, int, int, cred_t *);
static int sunslip_dlclose(queue_t *, int, cred_t *);
static int sunslip_dlwput(queue_t *, mblk_t *);
static int sunslip_dlwsrv(queue_t *);

static int sunslip_topen(queue_t *, dev_t *, int, int, cred_t *);
static int sunslip_tclose(queue_t *, int, cred_t *);
static int sunslip_trput(queue_t *, mblk_t *);
static int sunslip_twput(queue_t *, mblk_t *);

static void sunslip_info_ack(queue_t *, mblk_t *);
static void sunslip_bind(queue_t *, mblk_t *);
static void sunslip_unbind(queue_t *, mblk_t *);
static void sunslip_phys_addr(queue_t *, mblk_t *);
static void sunslip_ok_ack(queue_t *, mblk_t *, t_uscalar_t);
static void sunslip_error_ack(queue_t *, mblk_t *, t_uscalar_t,
    t_uscalar_t, t_uscalar_t);
static int sunslip_xmit(sunslip_state_t *, mblk_t *);
static void sunslip_rx_byte(sunslip_state_t *, unsigned char);
static void sunslip_rx_frame(sunslip_state_t *);

static struct module_info sunslip_dl_minfo = {
    0x5344, "sunslipd", 0, INFPSZ, SUNSLIP_HIWAT, SUNSLIP_LOWAT
};

static struct qinit sunslip_dlrinit = {
    NULL, NULL, sunslip_dlopen, sunslip_dlclose, NULL,
    &sunslip_dl_minfo, NULL
};

static struct qinit sunslip_dlwinit = {
    sunslip_dlwput, sunslip_dlwsrv, NULL, NULL, NULL,
    &sunslip_dl_minfo, NULL
};

static struct streamtab sunslip_dl_strtab = {
    &sunslip_dlrinit, &sunslip_dlwinit, NULL, NULL
};

static struct module_info sunslip_t_minfo = {
    0x5354, "sunslip", 0, INFPSZ, SUNSLIP_HIWAT, SUNSLIP_LOWAT
};

static struct qinit sunslip_trinit = {
    sunslip_trput, NULL, sunslip_topen, sunslip_tclose, NULL,
    &sunslip_t_minfo, NULL
};

static struct qinit sunslip_twinit = {
    sunslip_twput, NULL, NULL, NULL, NULL,
    &sunslip_t_minfo, NULL
};

static struct streamtab sunslip_t_strtab = {
    &sunslip_trinit, &sunslip_twinit, NULL, NULL
};

DDI_DEFINE_STREAM_OPS(sunslip_dev_ops, nulldev, nulldev,
    sunslip_attach, sunslip_detach, nodev, sunslip_getinfo,
    D_MP, &sunslip_dl_strtab);

static struct modldrv sunslip_modldrv = {
    &mod_driverops,
    "SunSlip RFC1055 DLPI driver 0.1",
    &sunslip_dev_ops
};

static fmodsw_t sunslip_fmodsw = {
    SUNSLIP_NAME,
    &sunslip_t_strtab,
    D_MP
};

static struct modlstrmod sunslip_modlstrmod = {
    &mod_strmodops,
    "SunSlip RFC1055 tty module 0.1",
    &sunslip_fmodsw
};

static struct modlinkage sunslip_modlinkage = {
    MODREV_1,
    &sunslip_modldrv,
    &sunslip_modlstrmod,
    NULL
};

int
_init(void)
{
    int e;

    bzero((caddr_t)&sunslip0, sizeof (sunslip0));
    sunslip0.dl_state = DL_UNBOUND;
    e = mod_install(&sunslip_modlinkage);
    if (e == 0)
        cmn_err(CE_NOTE, "sunslip: driver and tty module installed");
    return (e);
}

int
_fini(void)
{
    int e;

    if (sunslip0.dlpi_rq != NULL || sunslip0.tty_rq != NULL)
        return (EBUSY);
    e = mod_remove(&sunslip_modlinkage);
    if (e == 0)
        cmn_err(CE_NOTE, "sunslip: removed");
    return (e);
}

int
_info(struct modinfo *mip)
{
    return (mod_info(&sunslip_modlinkage, mip));
}

static int
sunslip_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result)
{
    dev_t dev = (dev_t)arg;
    (void)dip;

    switch (cmd) {
    case DDI_INFO_DEVT2DEVINFO:
        if (sunslip_dip == NULL)
            return (DDI_FAILURE);
        *result = sunslip_dip;
        return (DDI_SUCCESS);
    case DDI_INFO_DEVT2INSTANCE:
        *result = (void *)(long)(getminor(dev) - 1);
        return (DDI_SUCCESS);
    default:
        return (DDI_FAILURE);
    }
}

static int
sunslip_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
    if (cmd != DDI_ATTACH || ddi_get_instance(dip) != 0)
        return (DDI_FAILURE);

    if (ddi_create_minor_node(dip, "sunslip0", S_IFCHR, 1,
        DDI_PSEUDO, 0) != DDI_SUCCESS)
        return (DDI_FAILURE);

    sunslip_dip = dip;
    ddi_report_dev(dip);
    cmn_err(CE_NOTE, "sunslip0: attached");
    return (DDI_SUCCESS);
}

static int
sunslip_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
    if (cmd != DDI_DETACH)
        return (DDI_FAILURE);
    if (sunslip0.dlpi_rq != NULL || sunslip0.tty_rq != NULL)
        return (DDI_FAILURE);

    ddi_remove_minor_node(dip, NULL);
    sunslip_dip = NULL;
    return (DDI_SUCCESS);
}

static int
sunslip_dlopen(queue_t *rq, dev_t *devp, int oflag, int sflag, cred_t *crp)
{
    (void)oflag;
    (void)crp;

    if (sflag == MODOPEN || getminor(*devp) != 1)
        return (EINVAL);
    if (sunslip0.dlpi_rq != NULL && sunslip0.dlpi_rq != rq)
        return (EBUSY);

    rq->q_ptr = (caddr_t)&sunslip0;
    WR(rq)->q_ptr = (caddr_t)&sunslip0;
    sunslip0.dlpi_rq = rq;
    sunslip0.dl_state = DL_UNBOUND;
    qprocson(rq);
    cmn_err(CE_NOTE, "sunslip0: DLPI stream opened");
    return (0);
}

static int
sunslip_dlclose(queue_t *rq, int flag, cred_t *crp)
{
    sunslip_state_t *sl = (sunslip_state_t *)rq->q_ptr;
    (void)flag;
    (void)crp;

    qprocsoff(rq);
    if (sl != NULL && sl->dlpi_rq == rq) {
        sl->dlpi_rq = NULL;
        sl->dl_state = DL_UNBOUND;
        sl->sap = 0;
    }
    rq->q_ptr = NULL;
    WR(rq)->q_ptr = NULL;
    return (0);
}

static int
sunslip_dlwput(queue_t *q, mblk_t *mp)
{
    union DL_primitives *dlp;
    t_uscalar_t prim;

    switch (mp->b_datap->db_type) {
    case M_FLUSH:
        if (*mp->b_rptr & FLUSHW)
            flushq(q, FLUSHDATA);
        if (*mp->b_rptr & FLUSHR) {
            *mp->b_rptr &= ~FLUSHW;
            qreply(q, mp);
        } else {
            freemsg(mp);
        }
        return (0);

    case M_PROTO:
    case M_PCPROTO:
        if ((mp->b_wptr - mp->b_rptr) < sizeof (t_uscalar_t)) {
            freemsg(mp);
            return (0);
        }
        dlp = (union DL_primitives *)mp->b_rptr;
        prim = dlp->dl_primitive;
        switch (prim) {
        case DL_INFO_REQ:
            sunslip_info_ack(q, mp);
            break;
        case DL_BIND_REQ:
            sunslip_bind(q, mp);
            break;
        case DL_UNBIND_REQ:
            sunslip_unbind(q, mp);
            break;
        case DL_PHYS_ADDR_REQ:
            sunslip_phys_addr(q, mp);
            break;
        case DL_UNITDATA_REQ:
            if (((sunslip_state_t *)q->q_ptr)->dl_state != DL_IDLE) {
                sunslip_error_ack(q, mp, prim, DL_OUTSTATE, 0);
            } else if (mp->b_cont == NULL) {
                sunslip_error_ack(q, mp, prim, DL_BADDATA, 0);
            } else {
                (void)sunslip_xmit((sunslip_state_t *)q->q_ptr, mp);
            }
            break;
        default:
            cmn_err(CE_NOTE, "sunslip0: unsupported DLPI primitive 0x%lx",
                (unsigned long)prim);
            sunslip_error_ack(q, mp, prim, DL_NOTSUPPORTED, 0);
            break;
        }
        return (0);

    default:
        freemsg(mp);
        return (0);
    }
}

static int
sunslip_dlwsrv(queue_t *q)
{
    mblk_t *mp;

    while ((mp = getq(q)) != NULL) {
        if (!sunslip_xmit((sunslip_state_t *)q->q_ptr, mp)) {
            putbq(q, mp);
            break;
        }
    }
    return (0);
}

static void
sunslip_info_ack(queue_t *q, mblk_t *mp)
{
    sunslip_state_t *sl = (sunslip_state_t *)q->q_ptr;
    dl_info_ack_t *ack;
    mblk_t *nmp;

    freemsg(mp);
    nmp = allocb(DL_INFO_ACK_SIZE, BPRI_MED);
    if (nmp == NULL)
        return;

    nmp->b_datap->db_type = M_PCPROTO;
    ack = (dl_info_ack_t *)nmp->b_wptr;
    bzero((caddr_t)ack, sizeof (*ack));
    ack->dl_primitive = DL_INFO_ACK;
    ack->dl_max_sdu = SUNSLIP_MTU;
    ack->dl_min_sdu = 0;
    ack->dl_addr_length = 0;
    ack->dl_mac_type = DL_OTHER;
    ack->dl_current_state = sl->dl_state;
    ack->dl_sap_length = 0;
    ack->dl_service_mode = DL_CLDLS;
    ack->dl_provider_style = DL_STYLE1;
    ack->dl_addr_offset = 0;
    ack->dl_version = DL_VERSION_2;
    ack->dl_brdcst_addr_length = 0;
    ack->dl_brdcst_addr_offset = 0;
    ack->dl_growth = 0;
    cmn_err(CE_NOTE,
        "sunslip0: DL_INFO_ACK state=%lu style=%lu max_sdu=%lu",
        (unsigned long)ack->dl_current_state,
        (unsigned long)ack->dl_provider_style,
        (unsigned long)ack->dl_max_sdu);
    nmp->b_wptr += sizeof (*ack);
    qreply(q, nmp);
}

static void
sunslip_bind(queue_t *q, mblk_t *mp)
{
    sunslip_state_t *sl = (sunslip_state_t *)q->q_ptr;
    dl_bind_req_t *req;
    dl_bind_ack_t *ack;
    mblk_t *nmp;

    if ((mp->b_wptr - mp->b_rptr) < DL_BIND_REQ_SIZE) {
        sunslip_error_ack(q, mp, DL_BIND_REQ, DL_BADPRIM, 0);
        return;
    }
    if (sl->dl_state != DL_UNBOUND) {
        sunslip_error_ack(q, mp, DL_BIND_REQ, DL_OUTSTATE, 0);
        return;
    }

    req = (dl_bind_req_t *)mp->b_rptr;
    cmn_err(CE_NOTE,
        "sunslip0: DL_BIND_REQ sap=%lu service=0x%lx state=%lu",
        (unsigned long)req->dl_sap,
        (unsigned long)req->dl_service_mode,
        (unsigned long)sl->dl_state);
    if ((req->dl_service_mode & DL_CLDLS) == 0) {
        sunslip_error_ack(q, mp, DL_BIND_REQ, DL_UNSUPPORTED, 0);
        return;
    }

    sl->sap = req->dl_sap;
    sl->dl_state = DL_IDLE;
    freemsg(mp);

    nmp = allocb(DL_BIND_ACK_SIZE, BPRI_MED);
    if (nmp == NULL)
        return;
    nmp->b_datap->db_type = M_PCPROTO;
    ack = (dl_bind_ack_t *)nmp->b_wptr;
    bzero((caddr_t)ack, sizeof (*ack));
    ack->dl_primitive = DL_BIND_ACK;
    ack->dl_sap = sl->sap;
    ack->dl_addr_length = 0;
    ack->dl_addr_offset = 0;
    ack->dl_max_conind = 0;
    ack->dl_xidtest_flg = 0;
    cmn_err(CE_NOTE,
        "sunslip0: DL_BIND_ACK sap=%lu newstate=%lu",
        (unsigned long)ack->dl_sap,
        (unsigned long)sl->dl_state);
    nmp->b_wptr += sizeof (*ack);
    qreply(q, nmp);
}

static void
sunslip_unbind(queue_t *q, mblk_t *mp)
{
    sunslip_state_t *sl = (sunslip_state_t *)q->q_ptr;

    if (sl->dl_state != DL_IDLE) {
        sunslip_error_ack(q, mp, DL_UNBIND_REQ, DL_OUTSTATE, 0);
        return;
    }
    sl->dl_state = DL_UNBOUND;
    sl->sap = 0;
    sunslip_ok_ack(q, mp, DL_UNBIND_REQ);
}

static void
sunslip_phys_addr(queue_t *q, mblk_t *mp)
{
    dl_phys_addr_req_t *req;
    dl_phys_addr_ack_t *ack;
    mblk_t *nmp;

    if ((mp->b_wptr - mp->b_rptr) < DL_PHYS_ADDR_REQ_SIZE) {
        sunslip_error_ack(q, mp, DL_PHYS_ADDR_REQ, DL_BADPRIM, 0);
        return;
    }

    req = (dl_phys_addr_req_t *)mp->b_rptr;
    if (req->dl_addr_type != DL_CURR_PHYS_ADDR &&
        req->dl_addr_type != DL_FACT_PHYS_ADDR) {
        sunslip_error_ack(q, mp, DL_PHYS_ADDR_REQ, DL_NOTSUPPORTED, 0);
        return;
    }

    freemsg(mp);

    /*
     * SLIP has no link-layer/physical address.  Return a valid ACK with
     * zero address length rather than inventing Ethernet-like semantics.
     */
    nmp = allocb(DL_PHYS_ADDR_ACK_SIZE, BPRI_MED);
    if (nmp == NULL)
        return;

    nmp->b_datap->db_type = M_PCPROTO;
    ack = (dl_phys_addr_ack_t *)nmp->b_wptr;
    bzero((caddr_t)ack, sizeof (*ack));
    ack->dl_primitive = DL_PHYS_ADDR_ACK;
    ack->dl_addr_length = 0;
    ack->dl_addr_offset = 0;
    nmp->b_wptr += sizeof (*ack);
    qreply(q, nmp);
}

static void
sunslip_ok_ack(queue_t *q, mblk_t *mp, t_uscalar_t prim)
{
    dl_ok_ack_t *ack;

    if (mp->b_cont != NULL)
        freemsg(mp->b_cont);
    mp->b_cont = NULL;
    if ((mp->b_datap->db_lim - mp->b_datap->db_base) < DL_OK_ACK_SIZE) {
        freemsg(mp);
        mp = allocb(DL_OK_ACK_SIZE, BPRI_MED);
        if (mp == NULL)
            return;
    }
    mp->b_rptr = mp->b_datap->db_base;
    mp->b_wptr = mp->b_rptr;
    mp->b_datap->db_type = M_PCPROTO;
    ack = (dl_ok_ack_t *)mp->b_wptr;
    ack->dl_primitive = DL_OK_ACK;
    ack->dl_correct_primitive = prim;
    mp->b_wptr += sizeof (*ack);
    qreply(q, mp);
}

static void
sunslip_error_ack(queue_t *q, mblk_t *mp, t_uscalar_t prim,
    t_uscalar_t dlerr, t_uscalar_t unixerr)
{
    dl_error_ack_t *ack;

    if (mp->b_cont != NULL)
        freemsg(mp->b_cont);
    mp->b_cont = NULL;
    if ((mp->b_datap->db_lim - mp->b_datap->db_base) < DL_ERROR_ACK_SIZE) {
        freemsg(mp);
        mp = allocb(DL_ERROR_ACK_SIZE, BPRI_MED);
        if (mp == NULL)
            return;
    }
    mp->b_rptr = mp->b_datap->db_base;
    mp->b_wptr = mp->b_rptr;
    mp->b_datap->db_type = M_PCPROTO;
    ack = (dl_error_ack_t *)mp->b_wptr;
    ack->dl_primitive = DL_ERROR_ACK;
    ack->dl_error_primitive = prim;
    ack->dl_errno = dlerr;
    ack->dl_unix_errno = unixerr;
    mp->b_wptr += sizeof (*ack);
    qreply(q, mp);
}

static int
sunslip_xmit(sunslip_state_t *sl, mblk_t *mp)
{
    mblk_t *bp, *out;
    size_t len;
    unsigned char c;

    if (sl->tty_rq == NULL) {
        sl->oerrors++;
        freemsg(mp);
        return (1);
    }
    if (!canputnext(WR(sl->tty_rq)))
        return (0);

    len = msgdsize(mp->b_cont);
    if (len > SUNSLIP_MTU) {
        sl->oerrors++;
        freemsg(mp);
        return (1);
    }

    out = allocb((int)(2 * len + 1), BPRI_MED);
    if (out == NULL)
        return (0);
    out->b_datap->db_type = M_DATA;

    for (bp = mp->b_cont; bp != NULL; bp = bp->b_cont) {
        unsigned char *p;
        for (p = bp->b_rptr; p < bp->b_wptr; ++p) {
            c = *p;
            if (c == SL_END) {
                *out->b_wptr++ = SL_ESC;
                *out->b_wptr++ = SL_ESC_END;
            } else if (c == SL_ESC) {
                *out->b_wptr++ = SL_ESC;
                *out->b_wptr++ = SL_ESC_ESC;
            } else {
                *out->b_wptr++ = c;
            }
        }
    }
    *out->b_wptr++ = SL_END;

    sl->opackets++;
    putnext(WR(sl->tty_rq), out);
    freemsg(mp);
    return (1);
}

static int
sunslip_topen(queue_t *rq, dev_t *devp, int oflag, int sflag, cred_t *crp)
{
    (void)devp;
    (void)oflag;
    (void)crp;

    if (sflag != MODOPEN)
        return (EINVAL);
    if (sunslip0.tty_rq != NULL && sunslip0.tty_rq != rq)
        return (EBUSY);

    rq->q_ptr = (caddr_t)&sunslip0;
    WR(rq)->q_ptr = (caddr_t)&sunslip0;
    sunslip0.tty_rq = rq;
    sunslip0.escaped = 0;
    sunslip0.rx_overflow = 0;
    if (sunslip0.rx_mp != NULL) {
        freemsg(sunslip0.rx_mp);
        sunslip0.rx_mp = NULL;
    }
    qprocson(rq);
    cmn_err(CE_NOTE, "sunslip0: tty module pushed");
    return (0);
}

static int
sunslip_tclose(queue_t *rq, int flag, cred_t *crp)
{
    sunslip_state_t *sl = (sunslip_state_t *)rq->q_ptr;
    (void)flag;
    (void)crp;

    qprocsoff(rq);
    if (sl != NULL && sl->tty_rq == rq) {
        sl->tty_rq = NULL;
        if (sl->rx_mp != NULL) {
            freemsg(sl->rx_mp);
            sl->rx_mp = NULL;
        }
        sl->escaped = 0;
        sl->rx_overflow = 0;
    }
    rq->q_ptr = NULL;
    WR(rq)->q_ptr = NULL;
    return (0);
}

static int
sunslip_twput(queue_t *q, mblk_t *mp)
{
    putnext(q, mp);
    return (0);
}

static int
sunslip_trput(queue_t *q, mblk_t *mp)
{
    sunslip_state_t *sl = (sunslip_state_t *)q->q_ptr;
    mblk_t *bp;

    if (mp->b_datap->db_type != M_DATA) {
        putnext(q, mp);
        return (0);
    }

    for (bp = mp; bp != NULL; bp = bp->b_cont) {
        unsigned char *p;
        for (p = bp->b_rptr; p < bp->b_wptr; ++p)
            sunslip_rx_byte(sl, *p);
    }
    freemsg(mp);
    return (0);
}

static void
sunslip_rx_byte(sunslip_state_t *sl, unsigned char c)
{
    if (c == SL_END) {
        if (sl->rx_overflow) {
            if (sl->rx_mp != NULL) {
                freemsg(sl->rx_mp);
                sl->rx_mp = NULL;
            }
            sl->rx_overflow = 0;
            sl->escaped = 0;
            return;
        }
        sunslip_rx_frame(sl);
        sl->escaped = 0;
        return;
    }

    if (c == SL_ESC) {
        sl->escaped = 1;
        return;
    }

    if (sl->escaped) {
        sl->escaped = 0;
        if (c == SL_ESC_END)
            c = SL_END;
        else if (c == SL_ESC_ESC)
            c = SL_ESC;
    }

    if (sl->rx_overflow)
        return;

    if (sl->rx_mp == NULL) {
        sl->rx_mp = allocb(SUNSLIP_MTU, BPRI_MED);
        if (sl->rx_mp == NULL) {
            sl->ierrors++;
            sl->rx_overflow = 1;
            return;
        }
        sl->rx_mp->b_datap->db_type = M_DATA;
    }

    if (sl->rx_mp->b_wptr >= sl->rx_mp->b_datap->db_lim) {
        sl->ierrors++;
        sl->rx_overflow = 1;
        return;
    }
    *sl->rx_mp->b_wptr++ = c;
}

static void
sunslip_rx_frame(sunslip_state_t *sl)
{
    mblk_t *data, *proto;
    dl_unitdata_ind_t *ind;

    data = sl->rx_mp;
    sl->rx_mp = NULL;
    if (data == NULL || data->b_wptr == data->b_rptr) {
        if (data != NULL)
            freemsg(data);
        return;
    }

    if (sl->dlpi_rq == NULL || sl->dl_state != DL_IDLE ||
        !canputnext(sl->dlpi_rq)) {
        sl->ierrors++;
        freemsg(data);
        return;
    }

    proto = allocb(DL_UNITDATA_IND_SIZE, BPRI_MED);
    if (proto == NULL) {
        sl->ierrors++;
        freemsg(data);
        return;
    }

    proto->b_datap->db_type = M_PROTO;
    ind = (dl_unitdata_ind_t *)proto->b_wptr;
    bzero((caddr_t)ind, sizeof (*ind));
    ind->dl_primitive = DL_UNITDATA_IND;
    ind->dl_dest_addr_length = 0;
    ind->dl_dest_addr_offset = 0;
    ind->dl_src_addr_length = 0;
    ind->dl_src_addr_offset = 0;
    ind->dl_group_address = 0;
    proto->b_wptr += sizeof (*ind);
    proto->b_cont = data;
    sl->ipackets++;
    putnext(sl->dlpi_rq, proto);
}
