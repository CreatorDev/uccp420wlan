/*
 * File Name  : 80211_if.c
 *
 * This file is the glue layer between net/mac80211 and UMAC
 *
 * Copyright (c) 2011, 2012, 2013, 2014 Imagination Technologies Ltd.
 * All rights reserved
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/device.h>

#include <net/mac80211.h>
#include <net/cfg80211.h>
#include <net/ieee80211_radiotap.h>

#include <linux/udp.h>
#include <linux/ip.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>

#include "version.h"
#include "core.h"
#include "utils.h"

#include <linux/firmware.h>

#include <fwldr.h>

#define UCCP_DEBUG_80211IF(fmt, ...)                           \
do {                                                                    \
		if (uccp_debug & UCCP_DEBUG_80211IF)                       \
			pr_debug(fmt, ##__VA_ARGS__);  \
} while (0)

#define UCCP_DEBUG_CRYPTO(fmt, ...)                           \
do {                                                                    \
		if (uccp_debug & UCCP_DEBUG_CRYPTO)                       \
			pr_debug(fmt, ##__VA_ARGS__);  \
} while (0)

/* Its value will be the default mac address and it can only be updated with the
 * command line arguments
 */
unsigned int vht_support = 1;
module_param(vht_support, int, 0);
MODULE_PARM_DESC(vht_support, "Configure the 11ac support for this device");

static unsigned int ftm;
module_param(ftm, int, 0);
MODULE_PARM_DESC(ftm, "Factory Test Mode, should be used only for calibrations.");

unsigned int system_rev = 0x494D47; /*ASCII: IMG*/

int uccp_debug = UCCP_DEBUG_80211IF + UCCP_DEBUG_CRYPTO + UCCP_DEBUG_CORE;

module_param(uccp_debug, uint, 0);
MODULE_PARM_DESC(uccp_debug, " uccp_debug: Configure Debugging Mask");
static void uccp420_roc_complete_work(struct work_struct *work);
static void uccp420wlan_exit(void);
static int load_fw(struct ieee80211_hw *hw);
static char *uccp420_get_vif_name(int vif_idx);
int uccp_reinit;

#ifdef CONFIG_PM
unsigned char img_suspend_status;
#endif

#define CHAN2G(_freq, _idx)  {		\
	.band = IEEE80211_BAND_2GHZ,	\
	.center_freq = (_freq),		\
	.hw_value = (_idx),		\
	.max_power = 20,		\
}

#define CHAN5G(_freq, _idx, _flags) {	\
	.band = IEEE80211_BAND_5GHZ,	\
	.center_freq = (_freq),		\
	.hw_value = (_idx),		\
	.max_power = 20,		\
	.flags = (_flags),		\
}

struct wifi_dev {
	struct proc_dir_entry *umac_proc_dir_entry;
	struct wifi_params params;
	struct wifi_stats stats;
	struct ieee80211_hw *hw;
};

static struct wifi_dev *wifi;

static struct ieee80211_channel dsss_chantable[] = {
	CHAN2G(2412, 0),  /* Channel 1 */
	CHAN2G(2417, 1),  /* Channel 2 */
	CHAN2G(2422, 2),  /* Channel 3 */
	CHAN2G(2427, 3),  /* Channel 4 */
	CHAN2G(2432, 4),  /* Channel 5 */
	CHAN2G(2437, 5),  /* Channel 6 */
	CHAN2G(2442, 6),  /* Channel 7 */
	CHAN2G(2447, 7),  /* Channel 8 */
	CHAN2G(2452, 8),  /* Channel 9 */
	CHAN2G(2457, 9),  /* Channel 10 */
	CHAN2G(2462, 10), /* Channel 11 */
	CHAN2G(2467, 11), /* Channel 12 */
	CHAN2G(2472, 12), /* Channel 13 */
	CHAN2G(2484, 13), /* Channel 14 */
};

static struct ieee80211_channel ofdm_chantable[] = {
	CHAN5G(5180, 14, 0), /* Channel 36 */
	CHAN5G(5200, 15, 0), /* Channel 40 */
	CHAN5G(5220, 16, 0), /* Channel 44 */
	CHAN5G(5240, 17, 0), /* Channel 48 */
	CHAN5G(5260, 18, IEEE80211_CHAN_RADAR), /* Channel 52 */
	CHAN5G(5280, 19, IEEE80211_CHAN_RADAR), /* Channel 56 */
	CHAN5G(5300, 20, IEEE80211_CHAN_RADAR), /* Channel 60 */
	CHAN5G(5320, 21, IEEE80211_CHAN_RADAR), /* Channel 64 */
	CHAN5G(5500, 22, IEEE80211_CHAN_RADAR), /* Channel 100 */
	CHAN5G(5520, 23, IEEE80211_CHAN_RADAR), /* Channel 104 */
	CHAN5G(5540, 24, IEEE80211_CHAN_RADAR), /* Channel 108 */
	CHAN5G(5560, 25, IEEE80211_CHAN_RADAR), /* Channel 112 */
	CHAN5G(5580, 26, IEEE80211_CHAN_RADAR), /* Channel 116 */
	CHAN5G(5600, 27, IEEE80211_CHAN_RADAR), /* Channel 120 */
	CHAN5G(5620, 28, IEEE80211_CHAN_RADAR), /* Channel 124 */
	CHAN5G(5640, 29, IEEE80211_CHAN_RADAR), /* Channel 128 */
	CHAN5G(5660, 30, IEEE80211_CHAN_RADAR), /* Channel 132 */
	CHAN5G(5680, 31, IEEE80211_CHAN_RADAR), /* Channel 136 */
	CHAN5G(5700, 32, IEEE80211_CHAN_RADAR), /* Channel 140 */
	CHAN5G(5720, 33, IEEE80211_CHAN_RADAR), /* Channel 144 */
	CHAN5G(5745, 34, 0), /* Channel 149 */
	CHAN5G(5765, 35, 0), /* Channel 153 */
	CHAN5G(5785, 36, 0), /* Channel 157 */
	CHAN5G(5805, 37, 0), /* Channel 161 */
	CHAN5G(5825, 38, 0), /* Channel 165 */
};

static struct ieee80211_rate dsss_rates[] = {
	{ .bitrate = 10, .hw_value = 2},
	{ .bitrate = 20, .hw_value = 4,
	.flags = IEEE80211_RATE_SHORT_PREAMBLE},
	{ .bitrate = 55, .hw_value = 11,
	.flags = IEEE80211_RATE_SHORT_PREAMBLE},
	{ .bitrate = 110, .hw_value = 22,
	.flags = IEEE80211_RATE_SHORT_PREAMBLE},
	{ .bitrate = 60, .hw_value = 12},
	{ .bitrate = 90, .hw_value = 18},
	{ .bitrate = 120, .hw_value = 24},
	{ .bitrate = 180, .hw_value = 36},
	{ .bitrate = 240, .hw_value = 48},
	{ .bitrate = 360, .hw_value = 72},
	{ .bitrate = 480, .hw_value = 96},
	{ .bitrate = 540, .hw_value = 108}
};

static struct ieee80211_rate ofdm_rates[] = {
	{ .bitrate = 60, .hw_value = 12},
	{ .bitrate = 90, .hw_value = 18},
	{ .bitrate = 120, .hw_value = 24},
	{ .bitrate = 180, .hw_value = 36},
	{ .bitrate = 240, .hw_value = 48},
	{ .bitrate = 360, .hw_value = 72},
	{ .bitrate = 480, .hw_value = 96},
	{ .bitrate = 540, .hw_value = 108}
};

static struct ieee80211_supported_band band_2ghz = {
	.channels = dsss_chantable,
	.n_channels = ARRAY_SIZE(dsss_chantable),
	.band = IEEE80211_BAND_2GHZ,
	.bitrates = dsss_rates,
	.n_bitrates = ARRAY_SIZE(dsss_rates),
};

static struct ieee80211_supported_band band_5ghz = {
	.channels = ofdm_chantable,
	.n_channels = ARRAY_SIZE(ofdm_chantable),
	.band = IEEE80211_BAND_5GHZ,
	.bitrates = ofdm_rates,
	.n_bitrates = ARRAY_SIZE(ofdm_rates),
};


/* Interface combinations for Virtual interfaces */
static const struct ieee80211_iface_limit if_limit1[] = {
		{ .max = 2, .types = BIT(NL80211_IFTYPE_STATION)}
};

static const struct ieee80211_iface_limit if_limit2[] = {
		{ .max = 1, .types = BIT(NL80211_IFTYPE_STATION)},
		{ .max = 1, .types = BIT(NL80211_IFTYPE_AP) |
				     BIT(NL80211_IFTYPE_P2P_CLIENT) |
				     BIT(NL80211_IFTYPE_ADHOC) |
				     BIT(NL80211_IFTYPE_P2P_GO)}
};

static const struct ieee80211_iface_limit if_limit3[] = {
		{ .max = 2, .types = BIT(NL80211_IFTYPE_P2P_CLIENT)}
};

static const struct ieee80211_iface_limit if_limit4[] = {
		{ .max = 1, .types = BIT(NL80211_IFTYPE_ADHOC)},
		{ .max = 1, .types = BIT(NL80211_IFTYPE_P2P_CLIENT)}
};

#ifdef MULTI_CHAN_SUPPORT
static const struct ieee80211_iface_limit if_limit5[] = {
		{ .max = 1, .types = BIT(NL80211_IFTYPE_STATION)},
		{ .max = 1, .types = BIT(NL80211_IFTYPE_AP) |
				     BIT(NL80211_IFTYPE_P2P_GO) |
				     BIT(NL80211_IFTYPE_P2P_CLIENT)}
};
#endif

static const struct ieee80211_iface_limit if_limit6[] = {
		{ .max = 1, .types = BIT(NL80211_IFTYPE_AP)}
};

static const struct ieee80211_iface_limit if_limit7[] = {
		{ .max = 1, .types = BIT(NL80211_IFTYPE_STATION)}
};


static const struct ieee80211_iface_combination if_comb[] = {
	{ .limits = if_limit1,
	  .n_limits = ARRAY_SIZE(if_limit1),
	  .max_interfaces = 2,
	  .num_different_channels = 1},
	{ .limits = if_limit2,
	  .n_limits = ARRAY_SIZE(if_limit2),
	  .max_interfaces = 2,
	  .num_different_channels = 1},
	{ .limits = if_limit3,
	  .n_limits = ARRAY_SIZE(if_limit3),
	  .max_interfaces = 2,
	  .num_different_channels = 1},
#ifdef MULTI_CHAN_SUPPORT
	{ .limits = if_limit5,
	  .n_limits = ARRAY_SIZE(if_limit5),
	  .max_interfaces = 2,
	  .num_different_channels = 2},
	{ .limits = if_limit1,
	  .n_limits = ARRAY_SIZE(if_limit1),
	  .max_interfaces = 2,
	  .num_different_channels = 2},
#endif
	{ .limits = if_limit4,
	  .n_limits = ARRAY_SIZE(if_limit4),
	  .max_interfaces = 2,
	  .num_different_channels = 1},
#ifdef NOT_YET
	{ .limits = if_limit6,
	  .n_limits = ARRAY_SIZE(if_limit6),
	  .max_interfaces = 1,
	  .num_different_channels = 1,
	  .radar_detect_widths = BIT(NL80211_CHAN_WIDTH_20_NOHT) |
				 BIT(NL80211_CHAN_WIDTH_20) |
				 BIT(NL80211_CHAN_WIDTH_40) |
				 BIT(NL80211_CHAN_WIDTH_80) |
				 BIT(NL80211_CHAN_WIDTH_160)},
#endif
	{ .limits = if_limit7,
	  .n_limits = ARRAY_SIZE(if_limit7),
	  .max_interfaces = 1,
	  .num_different_channels = 1,
	  .radar_detect_widths = BIT(NL80211_CHAN_WIDTH_20_NOHT) |
				 BIT(NL80211_CHAN_WIDTH_20) |
				 BIT(NL80211_CHAN_WIDTH_40) |
				 BIT(NL80211_CHAN_WIDTH_80) |
				 BIT(NL80211_CHAN_WIDTH_160)}
};


/* For getting the dev pointer */
static struct class *hwsim_class;

static const struct wiphy_wowlan_support uccp_wowlan_support = {
	.flags = WIPHY_WOWLAN_ANY,
};

static int conv_str_to_byte(unsigned char *byte,
		     unsigned char *str,
		     int len)
{
	int  i, j = 0;
	unsigned char ch, val = 0;

	for (i = 0; i < (len * 2); i++) {
		/*convert to lower*/
		ch = ((str[i] >= 'A' && str[i] <= 'Z') ? str[i] + 32 : str[i]);

		if ((ch < '0' || ch > '9') && (ch < 'a' || ch > 'f'))
			return -1;

		if (ch >= '0' && ch <= '9')  /*check is digit*/
			ch = ch - '0';
		else
			ch = ch - 'a' + 10;

		val += ch;

		if (!(i%2))
			val <<= 4;
		else {
			byte[j] = val;
			j++;
			val = 0;
		}
	}

	return 0;
}


static void uccp420_roc_complete_work(struct work_struct *work)
{
	struct delayed_work *dwork = NULL;
	struct mac80211_dev *dev = NULL;
#ifdef MULTI_CHAN_SUPPORT
	struct umac_chanctx *off_chanctx = NULL;
	struct umac_vif *uvif = NULL, *tmp = NULL;
#endif
	struct tx_config *tx = NULL;
	u32 roc_queue = 0;
#ifdef MULTI_CHAN_SUPPORT
	bool need_offchan;
	int roc_off_chanctx_idx = -1;
	int chan_id = 0;
#endif

	dwork = container_of(work, struct delayed_work, work);
	dev = container_of(dwork, struct mac80211_dev, roc_complete_work);
	tx = &dev->tx;

	mutex_lock(&dev->mutex);
#ifdef MULTI_CHAN_SUPPORT
	need_offchan = dev->roc_params.need_offchan;
#endif

	roc_queue = tx_queue_unmap(UMAC_ROC_AC);
#ifdef MULTI_CHAN_SUPPORT
	roc_off_chanctx_idx = dev->roc_off_chanctx_idx;
#endif

	/* Stop the ROC queue */
	ieee80211_stop_queue(dev->hw, roc_queue);
	/* Unlock RCU immediately as we are freeing off_chanctx in this funciton
	 * only and because flush_vif_queues sleep
	 */
	rcu_read_lock();
#ifdef MULTI_CHAN_SUPPORT
	off_chanctx = rcu_dereference(dev->off_chanctx[roc_off_chanctx_idx]);
#endif
	rcu_read_unlock();

#ifdef MULTI_CHAN_SUPPORT
	list_for_each_entry_safe(uvif, tmp, &off_chanctx->vifs, list) {
		if (uvif == NULL || uvif->off_chanctx  == NULL)
			continue;
		/* Flush the TX queues */
		uccp420_flush_vif_queues(dev,
					 uvif,
					 uvif->off_chanctx->index,
					 BIT(UMAC_ROC_AC),
					 UMAC_VIF_CHANCTX_TYPE_OFF);


		spin_lock_bh(&tx->lock);
		spin_lock(&dev->chanctx_lock);

		/* ROC DONE: Move the channel context */
		if (uvif->chanctx)
			dev->curr_chanctx_idx = uvif->chanctx->index;
		else
			dev->curr_chanctx_idx = -1;

		spin_unlock(&dev->chanctx_lock);
		spin_unlock_bh(&tx->lock);

		if (need_offchan) {
			/* DEL from OFF chan list */
			list_del_init(&uvif->list);
			if (uvif->chanctx) {
				/* Add it back to OP chan list */
				list_add_tail(&uvif->list,
					      &uvif->chanctx->vifs);

				/* !need_offchan: In this case, the frames are
				 * transmitted, so trigger is not needed.
				 *
				 * need_offchan: In this case, frames are
				 * buffered so we need trigger in case no frames
				 * come from mac80211.
				 */
				/* Process OPER pending frames only.
				 * TXQ is flushed before start of ROC
				 */
				chan_id = uvif->chanctx->index;
				uccp420wlan_tx_proc_send_pend_frms_all(dev,
								       chan_id);
			}
			off_chanctx->nvifs--;
		}
		uvif->off_chanctx = NULL;
	}

	if (need_offchan)
		kfree(off_chanctx);


	rcu_assign_pointer(dev->off_chanctx[roc_off_chanctx_idx], NULL);
	dev->roc_off_chanctx_idx = -1;
#endif
	dev->roc_params.roc_in_progress = 0;

	if (dev->cancel_roc == 0) {
		ieee80211_remain_on_channel_expired(dev->hw);
		UCCP_DEBUG_ROC("%s:%d ROC STOPPED..\n", __func__, __LINE__);
	} else {
		dev->cancel_hw_roc_done = 1;
		dev->cancel_roc = 0;
		UCCP_DEBUG_ROC("%s:%d ROC CANCELLED..\n", __func__, __LINE__);
	}

	/* Start the ROC queue */
	ieee80211_wake_queue(dev->hw, roc_queue);
	mutex_unlock(&dev->mutex);
}


static void tx(struct ieee80211_hw *hw,
	       struct ieee80211_tx_control *txctl,
	       struct sk_buff *skb)
{
	struct mac80211_dev *dev = hw->priv;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct umac_vif *uvif;
	unsigned char null_bssid[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	struct iphdr *iphdr;
	unsigned char *pktgen_magic;
	unsigned int orig_pktgen_magic = 0x55e99bbe; /*Endianness 0xbe9be955*/
	struct umac_event_noa noa_event;
#ifdef MULTI_CHAN_SUPPORT
	int curr_chanctx_idx = -1;
#endif

	if (tx_info->control.vif == NULL) {
		UCCP_DEBUG_80211IF("%s: Dropping injected TX frame\n",
			 dev->name);
		dev_kfree_skb_any(skb);
		return;
	}

	uvif = (struct umac_vif *)(tx_info->control.vif->drv_priv);

	if (wifi->params.production_test) {
		if (((hdr->frame_control &
		      IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA) ||
		    (tx_info->control.vif == NULL))
			goto tx_status;

		iphdr = (struct iphdr *) skb_network_header(skb);
		if (iphdr->protocol == IPPROTO_UDP) {
			pktgen_magic = skb_transport_header(skb);
			pktgen_magic += sizeof(struct udphdr);
			/*If not PKTGEN, then drop it*/
			if (memcmp(pktgen_magic, &orig_pktgen_magic, 4) != 0) {
				UCCP_DEBUG_80211IF("%s:%d Prod_Mode: The pkt ",
						   __func__, __LINE__);
				UCCP_DEBUG_80211IF("is NOT PKTGEN so ");
				UCCP_DEBUG_80211IF("dropping it\n");
				goto tx_status;
			}
		} else {
			UCCP_DEBUG_80211IF("%s:%d prod_mode: The pkt is NOT ",
					   __func__, __LINE__);
			UCCP_DEBUG_80211IF("PKTGEN so dropping it\n");
			goto tx_status;
		}
	}
	if (ether_addr_equal(hdr->addr3, null_bssid))
		goto tx_status;

	if (uvif->vif->type != NL80211_IFTYPE_AP) {
		if ((dev->power_save == PWRSAVE_STATE_DOZE) &&
		    (((hdr->frame_control &
		      IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA) ||
			 is_bufferable_mgmt_frame(hdr)))
			hdr->frame_control |= IEEE80211_FCTL_PM;
	}

	if (uvif->noa_active) {
		memset(&noa_event, 0, sizeof(noa_event));
		noa_event.if_index = uvif->vif_index;
		uccp420wlan_noa_event(FROM_TX, &noa_event, dev, skb);
		return;
	}

#ifdef MULTI_CHAN_SUPPORT
	spin_lock_bh(&dev->chanctx_lock);
	curr_chanctx_idx = dev->curr_chanctx_idx;
	spin_unlock_bh(&dev->chanctx_lock);
#endif

	uccp420wlan_tx_frame(skb,
			     txctl->sta,
			     dev,
#ifdef MULTI_CHAN_SUPPORT
			     curr_chanctx_idx,
#endif
			     false);

	return;

tx_status:
	tx_info->flags |= IEEE80211_TX_STAT_ACK;
	tx_info->status.rates[0].count = 1;
	ieee80211_tx_status(hw, skb);
}

static int start(struct ieee80211_hw *hw)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)hw->priv;

	UCCP_DEBUG_80211IF("%s-80211IF: In start\n", dev->name);

	if ((wifi->params.fw_loading == 1) && load_fw(hw)) {
		UCCP_DEBUG_80211IF("%s-80211IF: FW load failed\n", dev->name);
		return -ENODEV;
	}

	mutex_lock(&dev->mutex);
	if ((uccp420wlan_core_init(dev, ftm)) < 0) {
		UCCP_DEBUG_80211IF("%s-80211IF: umac init failed\n", dev->name);
		mutex_unlock(&dev->mutex);
		return -ENODEV;
	}

	INIT_DELAYED_WORK(&dev->roc_complete_work, uccp420_roc_complete_work);

	dev->state = STARTED;
	memset(dev->params->pdout_voltage, 0,
	       sizeof(char) * MAX_AUX_ADC_SAMPLES);
#ifdef MULTI_CHAN_SUPPORT
	dev->roc_off_chanctx_idx = -1;
#endif
	mutex_unlock(&dev->mutex);

	return 0;
}

static void stop(struct ieee80211_hw *hw)
{
	struct mac80211_dev    *dev = (struct mac80211_dev *)hw->priv;

	UCCP_DEBUG_80211IF("%s-80211IF:In stop\n", dev->name);
	mutex_lock(&dev->mutex);
	uccp420wlan_core_deinit(dev, ftm);
	dev->state = STOPPED;
	mutex_unlock(&dev->mutex);

	hal_ops.reset_hal_params();

}

static int add_interface(struct ieee80211_hw *hw,
		struct ieee80211_vif *vif)
{
	struct mac80211_dev    *dev = hw->priv;
	struct ieee80211_vif *v;
	struct umac_vif   *uvif;
	int vif_index, iftype;

	mutex_lock(&dev->mutex);
	iftype = vif->type;
	v = vif;
	vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER;
	vif->driver_flags |= IEEE80211_VIF_SUPPORTS_UAPSD;

	if (dev->current_vif_count == wifi->params.num_vifs) {
		pr_err("%s: Exceeded Maximum supported VIF's cur:%d max: %d.\n",
		       __func__,
		       dev->current_vif_count,
		       wifi->params.num_vifs);

		mutex_unlock(&dev->mutex);
		return -ENOTSUPP;
	}

	if (!(iftype == NL80211_IFTYPE_STATION ||
	      iftype == NL80211_IFTYPE_ADHOC ||
	      iftype == NL80211_IFTYPE_AP)) {
		pr_err("Invalid Interface type\n");
		return -ENOTSUPP;
	}

	if (wifi->params.production_test) {
		if (dev->active_vifs || iftype != NL80211_IFTYPE_ADHOC) {
			mutex_unlock(&dev->mutex);
			return -EBUSY;
		}
	}

	for (vif_index = 0; vif_index < wifi->params.num_vifs; vif_index++) {
		if (!(dev->active_vifs & (1 << vif_index)))
			break;
	}

	/* This should never happen, we have taken care of this above */
	if (vif_index == wifi->params.num_vifs) {
		pr_err("%s: All VIF's are busy: %pM\n", __func__, vif->addr);
		mutex_unlock(&dev->mutex);
		return -EINVAL;
	}

	uvif = (struct umac_vif *)&v->drv_priv;
	uvif->vif_index = vif_index;
	uvif->vif = v;
	uvif->dev = dev;
	uvif->seq_no = 0;
	uccp420wlan_vif_add(uvif);
	dev->active_vifs |= (1 << vif_index);
	dev->current_vif_count++;

	if (iftype == NL80211_IFTYPE_ADHOC)
		dev->tx_last_beacon = 0;

	rcu_assign_pointer(dev->vifs[vif_index], v);
	synchronize_rcu();

	mutex_unlock(&dev->mutex);

	return 0;
}

static void remove_interface(struct ieee80211_hw *hw,
		struct ieee80211_vif *vif)
{
	struct mac80211_dev    *dev = hw->priv;
	struct ieee80211_vif *v;
	int vif_index;

	mutex_lock(&dev->mutex);
	v = vif;
	vif_index = ((struct umac_vif *)&v->drv_priv)->vif_index;

	uccp420wlan_vif_remove((struct umac_vif *)&v->drv_priv);
	dev->active_vifs &= ~(1 << vif_index);
	rcu_assign_pointer(dev->vifs[vif_index], NULL);
	synchronize_rcu();
	wifi->params.sync[vif_index].status = 0;
	dev->current_vif_count--;
	mutex_unlock(&dev->mutex);

}


static int config(struct ieee80211_hw *hw,
		unsigned int changed)
{
	struct mac80211_dev *dev = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;
	unsigned int pri_chnl_num;
	unsigned int freq_band;
	unsigned int ch_width;
	unsigned int center_freq = 0;
	unsigned int center_freq1 = 0;
	unsigned int center_freq2 = 0;
	int i = 0;
	int err = 0;
	struct ieee80211_vif *vif = NULL;

	UCCP_DEBUG_80211IF("%s-80211IF:In config\n", dev->name);

	mutex_lock(&dev->mutex);

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		dev->txpower = conf->power_level;
		uccp420wlan_prog_txpower(dev->txpower);
	}

	/* Check for change in channel */
	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		center_freq = conf->chandef.chan->center_freq;
		center_freq1 = conf->chandef.center_freq1;
		center_freq2 = conf->chandef.center_freq2;
		freq_band = conf->chandef.chan->band;
		ch_width = conf->chandef.width;

		pri_chnl_num = ieee80211_frequency_to_channel(center_freq);
		UCCP_DEBUG_80211IF("%s-80211IF:Primary Channel is %d\n",
				   dev->name,
				   pri_chnl_num);

		err = uccp420wlan_prog_channel(pri_chnl_num,
					       center_freq1, center_freq2,
					       ch_width,
#ifdef MULTI_CHAN_SUPPORT
					       0,
#endif
					       freq_band);

		if (err) {
			mutex_unlock(&dev->mutex);
			return err;
		}
		if (conf->radar_enabled) {
			UCCP_DEBUG_80211IF("RADAR Detection ENABLED on ");
			UCCP_DEBUG_80211IF("PriChannel=%d with ch_width=%d\n",
					   pri_chnl_num,
					   ch_width);

			uccp420wlan_prog_radar_detect(RADAR_DETECT_OP_START);
		}
	}

	/* Check for change in Power save state */
	for (i = 0; i < MAX_VIFS; i++) {
		if (!(changed & IEEE80211_CONF_CHANGE_PS))
			break;

		if (!(dev->active_vifs & (1 << i)))
			continue;

		/* When ROC is in progress, do not mess with
		 * PS state
		 */
		if (dev->roc_params.roc_in_progress)
			continue;

		if (wifi->params.disable_power_save)
			continue;

		if (conf->flags & IEEE80211_CONF_PS)
			dev->power_save = PWRSAVE_STATE_DOZE;
		else
			dev->power_save = PWRSAVE_STATE_AWAKE;

		UCCP_DEBUG_80211IF("%s-80211IF:PS state of VIF", dev->name);
		UCCP_DEBUG_80211IF(" %d changed to %d\n", i, dev->power_save);

		rcu_read_lock();
		vif = rcu_dereference(dev->vifs[i]);
		rcu_read_unlock();

		uccp420wlan_prog_ps_state(i,
					  vif->addr,
					  dev->power_save);
	}

	/* TODO: Make this global config as it effects all VIF's */
	for (i = 0; i < MAX_VIFS; i++) {
		if (!(changed & IEEE80211_CONF_CHANGE_SMPS))
			break;

		if (wifi->params.production_test == 1)
			break;

		if (!(dev->active_vifs & (1 << i)))
			continue;

		UCCP_DEBUG_80211IF("%s-80211IF:MIMO PS state of VIF %d -> %d\n",
				   dev->name,
				   i,
				   conf->smps_mode);

		rcu_read_lock();
		vif = rcu_dereference(dev->vifs[i]);
		rcu_read_unlock();

		uccp420wlan_prog_vif_smps(i,
					  vif->addr,
					  conf->smps_mode);
	}

	/* Check for change in Retry Limits */
	if (changed & IEEE80211_CONF_CHANGE_RETRY_LIMITS) {
		UCCP_DEBUG_80211IF("%s-80211IF:Retry Limits changed",
				   dev->name);
		UCCP_DEBUG_80211IF(" to %d and %d\n",
				   conf->short_frame_max_tx_count,
				   conf->long_frame_max_tx_count);
	}

	for (i = 0; i < MAX_VIFS; i++) {
		if (!(changed & IEEE80211_CONF_CHANGE_RETRY_LIMITS))
			break;

		if (!(dev->active_vifs & (1 << i)))
			continue;

		rcu_read_lock();
		vif = rcu_dereference(dev->vifs[i]);
		rcu_read_unlock();

		uccp420wlan_prog_short_retry(i,
					     vif->addr,
					     conf->short_frame_max_tx_count);
		uccp420wlan_prog_long_retry(i,
					    vif->addr,
					    conf->long_frame_max_tx_count);
	}

	mutex_unlock(&dev->mutex);
	return 0;
}


static u64 prepare_multicast(struct ieee80211_hw *hw,
			     struct netdev_hw_addr_list *mc_list)
{
	struct mac80211_dev *dev = hw->priv;
	int i;
	struct netdev_hw_addr *ha;
	int mc_count = 0;

	if (dev->state != STARTED)
		return 0;

	mc_count = netdev_hw_addr_list_count(mc_list);
	{
		if (mc_count > MCST_ADDR_LIMIT) {
			mc_count = 0;
			pr_warn("%s-80211IF:Disabling MCAST filter (cnt=%d)\n",
				dev->name, mc_count);
			goto out;
		}
	}
	UCCP_DEBUG_80211IF("%s-80211IF: Multicast filter count", dev->name);
	UCCP_DEBUG_80211IF("adding: %d removing: %d\n", mc_count,
			dev->mc_filter_count);

	if (dev->mc_filter_count > 0) {
		/* Remove all previous multicast addresses from the LMAC */
		for (i = 0; i < dev->mc_filter_count; i++)
			uccp420wlan_prog_mcast_addr_cfg(dev->mc_filters[i],
							WLAN_MCAST_ADDR_REM);
	}

	i = 0;

	netdev_hw_addr_list_for_each(ha, mc_list) {
		/* Prog the multicast address into the LMAC */
		uccp420wlan_prog_mcast_addr_cfg(ha->addr, WLAN_MCAST_ADDR_ADD);
		memcpy(dev->mc_filters[i], ha->addr, 6);
		i++;
	}

	dev->mc_filter_count = mc_count;
out:
	return mc_count;
}


static void configure_filter(struct ieee80211_hw *hw,
		unsigned int changed_flags,
		unsigned int *new_flags,
		u64 mc_count)
{
	struct mac80211_dev *dev = hw->priv;

	mutex_lock(&dev->mutex);

	changed_flags &= SUPPORTED_FILTERS;
	*new_flags &= SUPPORTED_FILTERS;

	if (dev->state != STARTED) {
		mutex_unlock(&dev->mutex);
		return;
	}

	if ((*new_flags & FIF_ALLMULTI) || (mc_count == 0)) {
		/* Disable the multicast filter in LMAC */
		UCCP_DEBUG_80211IF("%s-80211IF: Multicast filters disabled\n",
			       dev->name);
		uccp420wlan_prog_mcast_filter_control(MCAST_FILTER_DISABLE);
	} else if (mc_count) {
		/* Enable the multicast filter in LMAC */
		UCCP_DEBUG_80211IF("%s-80211IF: Multicast filters enabled\n",
			       dev->name);
		uccp420wlan_prog_mcast_filter_control(MCAST_FILTER_ENABLE);
	}

	if (changed_flags == 0)
		/* No filters which we support changed */
		goto out;

	if (wifi->params.production_test == 0) {
		if (*new_flags & FIF_BCN_PRBRESP_PROMISC) {
			/* Receive all beacons and probe responses */
			UCCP_DEBUG_80211IF("%s-80211IF: RCV ALL bcns\n",
				       dev->name);
			uccp420wlan_prog_rcv_bcn_mode(RCV_ALL_BCNS);
		} else {
			/* Receive only network beacons and probe responses */
			UCCP_DEBUG_80211IF("%s-80211IF: RCV NW bcns\n",
				       dev->name);
			uccp420wlan_prog_rcv_bcn_mode(RCV_ALL_NETWORK_ONLY);
		}
	}
out:
	if (wifi->params.production_test == 1) {
		UCCP_DEBUG_80211IF("%s-80211IF: RCV ALL bcns\n", dev->name);
		uccp420wlan_prog_rcv_bcn_mode(RCV_ALL_BCNS);
	}

	mutex_unlock(&dev->mutex);
}


static int conf_vif_tx(struct ieee80211_hw  *hw,
		       struct ieee80211_vif *vif,
		       unsigned short queue,
		       const struct ieee80211_tx_queue_params *txq_params)
{
	struct mac80211_dev *dev = hw->priv;
	int vif_index, vif_active;
	struct edca_params params;
	struct ieee80211_vif *vif_local = NULL;

	mutex_lock(&dev->mutex);

	for (vif_index = 0; vif_index < wifi->params.num_vifs; vif_index++) {
		if (!(dev->active_vifs & (1 << vif_index)))
			continue;

		rcu_read_lock();
		vif_local = rcu_dereference(dev->vifs[vif_index]);
		rcu_read_unlock();

		if (ether_addr_equal(vif_local->addr,
				     vif->addr))
			break;
	}

	if (WARN_ON(vif_index == wifi->params.num_vifs)) {
		mutex_unlock(&dev->mutex);
		return -EINVAL;
	}

	vif_active = 0;

	if ((dev->active_vifs & (1 << vif_index)))
		vif_active = 1;

	memset(&params, 0, sizeof(params));
	params.aifs = txq_params->aifs;
	params.txop = txq_params->txop;
	params.cwmin = txq_params->cw_min;
	params.cwmax = txq_params->cw_max;
	params.uapsd = txq_params->uapsd;

	uccp420wlan_vif_set_edca_params(queue,
					(struct umac_vif *)&vif->drv_priv,
					&params,
					vif_active);
	mutex_unlock(&dev->mutex);
	return 0;
}


static int set_key(struct ieee80211_hw *hw,
		   enum set_key_cmd cmd,
		   struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta,
		   struct ieee80211_key_conf *key_conf)
{

	struct umac_key sec_key;
	unsigned int result = 0;
	struct mac80211_dev *dev = hw->priv;
	unsigned int cipher_type, key_type;
	int vif_index;
	struct umac_vif *uvif;

	uvif = ((struct umac_vif *)&vif->drv_priv);

	memset(&sec_key, 0, sizeof(struct umac_key));

	switch (key_conf->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		sec_key.key = key_conf->key;
		cipher_type = CIPHER_TYPE_WEP40;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		sec_key.key = key_conf->key;
		cipher_type = CIPHER_TYPE_WEP104;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		key_conf->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
		/* We get the key in the following form:
		 * KEY (16 bytes) - TX MIC (8 bytes) - RX MIC (8 bytes)
		 */
		sec_key.key = key_conf->key;
		sec_key.tx_mic = key_conf->key + 16;
		sec_key.rx_mic = key_conf->key + 24;
		cipher_type = CIPHER_TYPE_TKIP;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		sec_key.key = key_conf->key;
		cipher_type = CIPHER_TYPE_CCMP;
		break;
	default:
		result = -EOPNOTSUPP;
		mutex_unlock(&dev->mutex);
		goto out;
	}

	vif_index = ((struct umac_vif *)&vif->drv_priv)->vif_index;

	mutex_lock(&dev->mutex);

	if (cmd == SET_KEY) {
		key_conf->hw_key_idx = 0; /* Don't really use this */

		/* This flag indicate that it requires IV generation */
		key_conf->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;


		if (cipher_type == CIPHER_TYPE_WEP40 ||
		    cipher_type == CIPHER_TYPE_WEP104) {
			UCCP_DEBUG_CRYPTO("%s-80211IF: ADD IF KEY (WEP).",
					  dev->name);
			UCCP_DEBUG_CRYPTO(" vif_index = %d,", vif_index);
			UCCP_DEBUG_CRYPTO(" keyidx = %d, cipher_type = %d\n",
					  key_conf->keyidx, cipher_type);

			uccp420wlan_prog_if_key(vif_index,
						vif->addr,
						KEY_CTRL_ADD,
						key_conf->keyidx,
						cipher_type,
						&sec_key);
		} else if (sta) {
			sec_key.peer_mac = sta->addr;

			if (key_conf->flags & IEEE80211_KEY_FLAG_PAIRWISE)
				key_type = KEY_TYPE_UCAST;
			else
				key_type = KEY_TYPE_BCAST;
			UCCP_DEBUG_CRYPTO("%s-80211IF: ADD PEER KEY (WPA/WPA2)",
					  dev->name);
			UCCP_DEBUG_CRYPTO(" vif_index = %d,", vif_index);
			UCCP_DEBUG_CRYPTO(" keyidx = %d, keytype = %d,",
					  key_conf->keyidx, key_type);
			UCCP_DEBUG_CRYPTO(" cipher_type = %d\n", cipher_type);

			uccp420wlan_prog_peer_key(vif_index,
						  vif->addr,
						  KEY_CTRL_ADD,
						  key_conf->keyidx,
						  key_type,
						  cipher_type,
						  &sec_key);
		} else {
			key_type = KEY_TYPE_BCAST;

			if (vif->type == NL80211_IFTYPE_STATION) {
				sec_key.peer_mac =
					(unsigned char *)vif->bss_conf.bssid;

				memcpy(uvif->bssid,
				       (vif->bss_conf.bssid),
				       ETH_ALEN);
				UCCP_DEBUG_CRYPTO("%s-80211IF: ADD PEER KEY ",
						  dev->name);
				UCCP_DEBUG_CRYPTO("(BCAST-STA). vif_index = %d",
						  vif_index);
				UCCP_DEBUG_CRYPTO(", keyidx = %d, keytype = %d",
						key_conf->keyidx, key_type);
				UCCP_DEBUG_CRYPTO(", cipher_type = %d\n",
						  cipher_type);

				uccp420wlan_prog_peer_key(vif_index,
							  vif->addr,
							  KEY_CTRL_ADD,
							  key_conf->keyidx,
							  key_type, cipher_type,
							  &sec_key);

			} else if (vif->type == NL80211_IFTYPE_AP) {
				UCCP_DEBUG_CRYPTO("%s-80211IF: ADD IF KEY ",
						  dev->name);
				UCCP_DEBUG_CRYPTO("(BCAST-AP). vif_index = %d",
						  vif_index);
				UCCP_DEBUG_CRYPTO(", keyidx = %d",
						  key_conf->keyidx);
				UCCP_DEBUG_CRYPTO(", cipher_type = %d\n",
						  cipher_type);

				uccp420wlan_prog_if_key(vif_index,
							vif->addr,
							KEY_CTRL_ADD,
							key_conf->keyidx,
							cipher_type,
							&sec_key);
			} else {
				/* ADHOC */
				/* TODO: Check this works for IBSS RSN */
				UCCP_DEBUG_CRYPTO("%s-80211IF: ADD IF KEY ",
						  dev->name);
				UCCP_DEBUG_CRYPTO("(BCAST-IBSS).vif_index = %d",
						  vif_index);
				UCCP_DEBUG_CRYPTO(", keyidx = %d",
						  key_conf->keyidx);
				UCCP_DEBUG_CRYPTO(", cipher_type = %d\n",
						  cipher_type);

				uccp420wlan_prog_if_key(vif_index,
							vif->addr,
							KEY_CTRL_ADD,
							key_conf->keyidx,
							cipher_type,
							&sec_key);
			}
		}
	} else if (cmd == DISABLE_KEY) {
		if ((cipher_type == CIPHER_TYPE_WEP40) ||
		    (cipher_type == CIPHER_TYPE_WEP104)) {
			uccp420wlan_prog_if_key(vif_index,
						vif->addr,
						KEY_CTRL_DEL,
						key_conf->keyidx,
						cipher_type,
						&sec_key);
			UCCP_DEBUG_CRYPTO("%s-80211IF: DEL IF KEY (WEP).",
					  dev->name);
			UCCP_DEBUG_CRYPTO(" vif_index = %d, keyidx = %d",
					  vif_index, key_conf->keyidx);
			UCCP_DEBUG_CRYPTO(", cipher_type = %d\n", cipher_type);
		} else if (sta) {
			sec_key.peer_mac = sta->addr;

			if (key_conf->flags & IEEE80211_KEY_FLAG_PAIRWISE)
				key_type = KEY_TYPE_UCAST;
			else
				key_type = KEY_TYPE_BCAST;
			UCCP_DEBUG_CRYPTO("%s-80211IF: DEL IF KEY (WPA/WPA2).",
					  dev->name);
			UCCP_DEBUG_CRYPTO(" vif_index = %d, keyidx = %d",
					  vif_index, key_conf->keyidx);
			UCCP_DEBUG_CRYPTO(", cipher_type = %d\n", cipher_type);

			uccp420wlan_prog_peer_key(vif_index,
						  vif->addr,
						  KEY_CTRL_DEL,
						  key_conf->keyidx,
						  key_type,
						  cipher_type,
						  &sec_key);
		} else {
			if (vif->type == NL80211_IFTYPE_STATION) {
				sec_key.peer_mac = uvif->bssid;
				UCCP_DEBUG_CRYPTO("%s-80211IF: DEL IF KEY ",
						  dev->name);
				UCCP_DEBUG_CRYPTO("(BCAST-STA). vif_index = %d",
						  vif_index);
				UCCP_DEBUG_CRYPTO(", keyidx = %d",
						  key_conf->keyidx);
				UCCP_DEBUG_CRYPTO(", cipher_type = %d\n",
						  cipher_type);

				uccp420wlan_prog_peer_key(vif_index,
							  vif->addr,
							  KEY_CTRL_DEL,
							  key_conf->keyidx,
							  KEY_TYPE_BCAST,
							  cipher_type,
							  &sec_key);

			} else if (vif->type == NL80211_IFTYPE_AP) {
				UCCP_DEBUG_CRYPTO("%s-80211IF: DEL IF KEY ",
						  dev->name);
				UCCP_DEBUG_CRYPTO("(BCAST-AP). vif_index = %d",
						  vif_index);
				UCCP_DEBUG_CRYPTO(", keyidx = %d",
						  key_conf->keyidx);
				UCCP_DEBUG_CRYPTO(", cipher_type = %d\n",
						  cipher_type);

				uccp420wlan_prog_if_key(vif_index,
							vif->addr,
							KEY_CTRL_DEL,
							key_conf->keyidx,
							cipher_type,
							&sec_key);
			} else {
				UCCP_DEBUG_CRYPTO("%s-80211IF: DEL IF KEY ",
						  dev->name);
				UCCP_DEBUG_CRYPTO("(BCAST-IBSS).vif_index = %d",
						  vif_index);
				UCCP_DEBUG_CRYPTO(", keyidx = %d",
						  key_conf->keyidx);
				UCCP_DEBUG_CRYPTO(", cipher_type = %d\n",
						  cipher_type);

				uccp420wlan_prog_if_key(vif_index,
							vif->addr,
							KEY_CTRL_DEL,
							key_conf->keyidx,
							cipher_type,
							&sec_key);
			}
		}
	}

	mutex_unlock(&dev->mutex);

out:
	return result;
}


static void bss_info_changed(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *bss_conf,
			     unsigned int changed)
{
	struct mac80211_dev   *dev = hw->priv;

	mutex_lock(&dev->mutex);

	if (wifi->params.production_test || wifi->params.disable_beacon_ibss) {
		/* Disable beacon generation when running pktgen
		 * for performance
		 */
		changed &= ~BSS_CHANGED_BEACON_INT;
		changed &= ~BSS_CHANGED_BEACON_ENABLED;
	}

	uccp420wlan_vif_bss_info_changed((struct umac_vif *)&vif->drv_priv,
					 bss_conf,
					 changed);
	mutex_unlock(&dev->mutex);
}


static void setup_ht_cap(struct ieee80211_sta_ht_cap *ht_info)
{
	int i;

	memset(ht_info, 0, sizeof(*ht_info));
	ht_info->ht_supported = true;
	pr_info("SETUP HT CALLED\n");
#if 0
	ht_info->cap |= IEEE80211_HT_CAP_DSSSCCK40;
#endif
	ht_info->cap = 0;
	ht_info->cap |= IEEE80211_HT_CAP_MAX_AMSDU;
	ht_info->cap |= IEEE80211_HT_CAP_SGI_40;
	ht_info->cap |= IEEE80211_HT_CAP_SGI_20;
	ht_info->cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
	ht_info->cap |= IEEE80211_HT_CAP_GRN_FLD;
	ht_info->cap |= IEEE80211_HT_CAP_LDPC_CODING;
	ht_info->cap |= IEEE80211_HT_CAP_TX_STBC;
	ht_info->cap |= (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT);
	ht_info->cap |= IEEE80211_HT_CAP_LSIG_TXOP_PROT;
	/*We support SMPS*/

	ht_info->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	ht_info->ampdu_density = IEEE80211_HT_MPDU_DENSITY_4;

	memset(&ht_info->mcs, 0, sizeof(ht_info->mcs));

	if (wifi->params.max_tx_streams != wifi->params.max_rx_streams) {
		ht_info->mcs.tx_params |= IEEE80211_HT_MCS_TX_RX_DIFF;
		ht_info->mcs.tx_params |= ((wifi->params.max_tx_streams - 1)
				<< IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT);
	}

	for (i = 0; i < wifi->params.max_rx_streams; i++)
		ht_info->mcs.rx_mask[i] = 0xff;
	ht_info->mcs.rx_mask[4] = 0x1;

	ht_info->mcs.tx_params |= IEEE80211_HT_MCS_TX_DEFINED;
}


#define IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT 13
#define IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT 16
static void setup_vht_cap(struct ieee80211_sta_vht_cap *vht_info)
{
	if (!vht_support)
		return;

	memset(vht_info, 0, sizeof(*vht_info));
	vht_info->vht_supported = true;
	pr_info("SETUP VHT CALLED\n");

	vht_info->cap = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
			/*64KB Rx buffer size*/
			(3 <<
			IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT) |
#if 0
			IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE |
			IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
			(1 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT) |
			(1 << IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT) |
#endif
			IEEE80211_VHT_CAP_SHORT_GI_80 |
			IEEE80211_VHT_CAP_RXLDPC |
			IEEE80211_VHT_CAP_TXSTBC |
			IEEE80211_VHT_CAP_RXSTBC_1 |
			IEEE80211_VHT_CAP_HTC_VHT;
	/* 1x1 */
	if ((wifi->params.max_tx_streams == 1) &&
	    (wifi->params.max_rx_streams == 1)) {
		vht_info->vht_mcs.rx_mcs_map =
			((IEEE80211_VHT_MCS_SUPPORT_0_7) << (2*0)) |
			((IEEE80211_VHT_MCS_NOT_SUPPORTED) << (2*1)) |
			((IEEE80211_VHT_MCS_NOT_SUPPORTED) << (2*2)) |
			((IEEE80211_VHT_MCS_NOT_SUPPORTED) << (2*3)) |
			((IEEE80211_VHT_MCS_NOT_SUPPORTED) << (2*4)) |
			((IEEE80211_VHT_MCS_NOT_SUPPORTED) << (2*5)) |
			((IEEE80211_VHT_MCS_NOT_SUPPORTED) << (2*6)) |
			((IEEE80211_VHT_MCS_NOT_SUPPORTED) << (2*7));
	}

	/*2x2 */
	if ((wifi->params.max_tx_streams == 2) &&
	    (wifi->params.max_rx_streams == 2)) {
		vht_info->vht_mcs.rx_mcs_map =
			((IEEE80211_VHT_MCS_SUPPORT_0_7) << (2*0)) |
			((IEEE80211_VHT_MCS_SUPPORT_0_7) << (2*1)) |
			((IEEE80211_VHT_MCS_NOT_SUPPORTED) << (2*2)) |
			((IEEE80211_VHT_MCS_NOT_SUPPORTED) << (2*3)) |
			((IEEE80211_VHT_MCS_NOT_SUPPORTED) << (2*4)) |
			((IEEE80211_VHT_MCS_NOT_SUPPORTED) << (2*5)) |
			((IEEE80211_VHT_MCS_NOT_SUPPORTED) << (2*6)) |
			((IEEE80211_VHT_MCS_NOT_SUPPORTED) << (2*7));
	}

	vht_info->vht_mcs.tx_mcs_map = vht_info->vht_mcs.rx_mcs_map;
}


static void init_hw(struct ieee80211_hw *hw)
{
	struct mac80211_dev  *dev = (struct mac80211_dev *)hw->priv;
	int num_if_comb = 0;

	/* Supported Interface Types and other Default values*/
	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				     BIT(NL80211_IFTYPE_ADHOC) |
				     BIT(NL80211_IFTYPE_AP) |
				     BIT(NL80211_IFTYPE_P2P_CLIENT) |
				     BIT(NL80211_IFTYPE_P2P_GO);

	hw->wiphy->iface_combinations = if_comb;

	num_if_comb = (sizeof(if_comb) /
		       sizeof(struct ieee80211_iface_combination));
	hw->wiphy->n_iface_combinations = num_if_comb;

	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, SUPPORTS_PS);
	ieee80211_hw_set(hw, HOST_BROADCAST_PS_BUFFERING);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(hw, MFP_CAPABLE);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);

	if (wifi->params.dot11a_support)
		ieee80211_hw_set(hw, SPECTRUM_MGMT);

	ieee80211_hw_set(hw, SUPPORTS_PER_STA_GTK);
	ieee80211_hw_set(hw, CONNECTION_MONITOR);
	ieee80211_hw_set(hw, CHANCTX_STA_CSA);

	hw->wiphy->max_scan_ssids = MAX_NUM_SSIDS; /* 4 */
	 /* Low priority bg scan */
	hw->wiphy->features |= NL80211_FEATURE_LOW_PRIORITY_SCAN;
	hw->wiphy->max_scan_ie_len = IEEE80211_MAX_DATA_LEN;
	hw->max_listen_interval = 10;
	hw->wiphy->max_remain_on_channel_duration = 5000; /*ROC*/
	hw->offchannel_tx_hw_queue = WLAN_AC_VO;
	hw->max_rates = 4;
	hw->max_rate_tries = 5;
	hw->queues = 4;

	/* Size */
	hw->extra_tx_headroom = 0;
	hw->vif_data_size = sizeof(struct umac_vif);
	hw->sta_data_size = sizeof(struct umac_sta);
#ifdef MULTI_CHAN_SUPPORT
	hw->chanctx_data_size = sizeof(struct umac_chanctx);
#endif

	if (wifi->params.dot11g_support) {
		hw->wiphy->bands[IEEE80211_BAND_2GHZ] = &band_2ghz;
		setup_ht_cap(&hw->wiphy->bands[IEEE80211_BAND_2GHZ]->ht_cap);
	}

	if (wifi->params.dot11a_support) {
		if (vht_support)
			setup_vht_cap(&band_5ghz.vht_cap);
		hw->wiphy->bands[IEEE80211_BAND_5GHZ] = &band_5ghz;
		setup_ht_cap(&hw->wiphy->bands[IEEE80211_BAND_5GHZ]->ht_cap);
	}

	memset(hw->wiphy->addr_mask, 0, sizeof(hw->wiphy->addr_mask));

	if (wifi->params.num_vifs == 1) {
		hw->wiphy->addresses = NULL;
		SET_IEEE80211_PERM_ADDR(hw, dev->if_mac_addresses[0].addr);
	} else {
		hw->wiphy->n_addresses = wifi->params.num_vifs;
		hw->wiphy->addresses = dev->if_mac_addresses;
	}

	hw->wiphy->flags |= WIPHY_FLAG_AP_UAPSD;
	hw->wiphy->flags |= WIPHY_FLAG_IBSS_RSN;
	hw->wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
#ifdef notyet
	hw->wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
#endif
	hw->wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;

	if (!wifi->params.disable_power_save &&
	    !wifi->params.disable_sm_power_save) {
		/* SMPS Support both Static and Dynamic */
		hw->wiphy->features |= NL80211_FEATURE_STATIC_SMPS;
		hw->wiphy->features |= NL80211_FEATURE_DYNAMIC_SMPS;
	}

#ifdef CONFIG_PM
	hw->wiphy->wowlan = &uccp_wowlan_support;
#endif
}


static int ampdu_action(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				enum ieee80211_ampdu_mlme_action action,
				struct ieee80211_sta *sta,
				u16 tid, u16 *ssn, u8 buf_size, bool amsdu)
{
	int ret = 0;
	unsigned int val = 0;
	struct mac80211_dev *dev = (struct mac80211_dev *)hw->priv;

	UCCP_DEBUG_80211IF("%s-80211IF: ampdu action started\n",
			((struct mac80211_dev *)(hw->priv))->name);
		/* TODO */
	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		{
		val = tid | TID_INITIATOR_AP;
		dev->tid_info[val].tid_state = TID_STATE_AGGR_START;
		dev->tid_info[val].ssn = *ssn;
		uccp420wlan_prog_ba_session_data(1,
						 tid,
						 &dev->tid_info[val].ssn,
						 1,
						 vif->addr,
				   (unsigned char *)(vif->bss_conf.bssid));
		}
		break;
	case IEEE80211_AMPDU_RX_STOP:
		{
		val = tid | TID_INITIATOR_AP;
		dev->tid_info[val].tid_state = TID_STATE_AGGR_STOP;
		uccp420wlan_prog_ba_session_data(0,
						 tid,
						 &dev->tid_info[val].ssn,
						 1,
						 vif->addr,
				   (unsigned char *)(vif->bss_conf.bssid));
		}
		break;
	case IEEE80211_AMPDU_TX_START:
		{
		val = tid | TID_INITIATOR_STA;
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		dev->tid_info[val].tid_state = TID_STATE_AGGR_START;
		dev->tid_info[val].ssn = *ssn;
		}
		break;
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
	case IEEE80211_AMPDU_TX_STOP_CONT:
		{
		val = tid | TID_INITIATOR_STA;
		dev->tid_info[val].tid_state = TID_STATE_AGGR_STOP;
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		}
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		{
		val = tid | TID_INITIATOR_STA;
		dev->tid_info[val].tid_state = TID_STATE_AGGR_OPERATIONAL;
		}
		break;
	default:
		pr_err("%s: Invalid command, ignoring\n",
		       __func__);
	}
	return ret;
}


static int set_antenna(struct ieee80211_hw *hw, u32 tx_ant, u32 rx_ant)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)hw->priv;

	/* Maximum no of antenna supported =2 */
	if (!tx_ant || (tx_ant & ~3) || !rx_ant || (rx_ant & ~3))
		return -EINVAL;

	dev->tx_antenna = (tx_ant & 3);

	return 0;
}


static int remain_on_channel(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_channel *channel,
			     int duration,
			     enum ieee80211_roc_type type)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)hw->priv;
	unsigned int pri_chnl_num =
		ieee80211_frequency_to_channel(channel->center_freq);
#ifdef MULTI_CHAN_SUPPORT
	struct umac_vif *uvif = (struct umac_vif *)vif->drv_priv;
	struct umac_chanctx *off_chanctx = NULL;
	int off_chanctx_id = 0, i = 0;
	struct tx_config *tx = &dev->tx;
	u32 hw_queue_map = 0;
	struct ieee80211_chanctx_conf *vif_chanctx;
	bool need_offchan = true;
#endif

	mutex_lock(&dev->mutex);
	UCCP_DEBUG_ROC("%s:%d The Params are:",
					__func__,
					__LINE__);
	UCCP_DEBUG_ROC(" channel:%d duration:%d type: %d\n",
			ieee80211_frequency_to_channel(channel->center_freq),
			duration,
			type);

	if (dev->roc_params.roc_in_progress) {
		UCCP_DEBUG_ROC("%s:%d Dropping roc...Busy\n",
				__func__,
				__LINE__);
		mutex_unlock(&dev->mutex);
		return -EBUSY;
	}

#ifdef MULTI_CHAN_SUPPORT
	if (dev->num_active_chanctx == 2) {
		UCCP_DEBUG_ROC("%s:%d RoC is not supported in TSMC Mode\n",
				__func__,
				__LINE__);
		mutex_unlock(&dev->mutex);
		return -ENOTSUPP;
	}
#endif

	/* Inform FW that ROC is started:
	 * For pure TX we send OFFCHANNEL_TX so that driver can terminate ROC
	 * For Tx + Rx we use NORMAL, FW will terminate ROC based on duration.
	 */
	if (duration != 10 && type == ROC_TYPE_OFFCHANNEL_TX)
		type = ROC_TYPE_NORMAL;

#ifdef MULTI_CHAN_SUPPORT
	/* uvif is in connected state
	 */
	if (uvif->chanctx) {
		rcu_read_lock();

		vif_chanctx =
			rcu_dereference(dev->chanctx[uvif->chanctx->index]);

		/* AS ROC frames are MGMT frames, checking only for Primary
		 * Channel.
		 */
		if (vif_chanctx->def.chan->center_freq == channel->center_freq)
			need_offchan = false;

		rcu_read_unlock();
	}

	UCCP_DEBUG_ROC("%s:%d need_offchan: %d\n",
			__func__,
			__LINE__,
			need_offchan);
	dev->roc_params.need_offchan = need_offchan;

	if (need_offchan) {
		/* Different chan context than the uvif */
		off_chanctx = kmalloc(sizeof(struct umac_chanctx),
				      GFP_KERNEL);

		if (!off_chanctx) {
			pr_err("%s: Unable to alloc mem for channel context\n",
			       __func__);
			mutex_unlock(&dev->mutex);
			return -ENOMEM;
		}

		/** Currently OFFCHAN is limited to handling ROC case
		 *  but it is meant for a generic case.
		 *  ideally we should look for existing offchan context
		 *  and re-use/create.
		 */
		for (i = 0; i < MAX_OFF_CHANCTX; i++) {
			if (!dev->off_chanctx[i]) {
				off_chanctx_id = i;
				break;
			}
		}

		if (uvif->chanctx) {
			ieee80211_stop_queues(hw);

			hw_queue_map = BIT(WLAN_AC_BK) |
				BIT(WLAN_AC_BE) |
				BIT(WLAN_AC_VI) |
				BIT(WLAN_AC_VO) |
				BIT(WLAN_AC_BCN);

			uccp420_flush_vif_queues(dev,
					uvif,
					uvif->chanctx->index,
					hw_queue_map,
					UMAC_VIF_CHANCTX_TYPE_OPER);
		}


		off_chanctx->index = OFF_CHANCTX_IDX_BASE + off_chanctx_id;
		dev->roc_off_chanctx_idx = off_chanctx_id;
		INIT_LIST_HEAD(&off_chanctx->vifs);
		off_chanctx->nvifs = 0;

		if (uvif->chanctx) {
			/* Delete the uvif from OP channel list */
			list_del_init(&uvif->list);
		}
		/* Add the vif to the off_chanctx */
		list_add_tail(&uvif->list, &off_chanctx->vifs);
		off_chanctx->nvifs++;
		rcu_assign_pointer(dev->off_chanctx[off_chanctx_id],
				   off_chanctx);
		synchronize_rcu();


		/* Move the channel context */
		spin_lock_bh(&dev->chanctx_lock);
		dev->curr_chanctx_idx = off_chanctx->index;
		spin_unlock_bh(&dev->chanctx_lock);
	} else {
		/* Same channel context, just update off_chanctx
		 * to chanctx
		 */
		off_chanctx = uvif->chanctx;

		for (i = 0; i < MAX_OFF_CHANCTX; i++) {
			if (!dev->off_chanctx[i]) {
				off_chanctx_id = i;
				break;
			}
		}
		dev->roc_off_chanctx_idx = off_chanctx->index;
		rcu_assign_pointer(dev->off_chanctx[off_chanctx_id],
				   off_chanctx);
		synchronize_rcu();
	}
	spin_lock_bh(&tx->lock);
	uvif->off_chanctx = off_chanctx;
	spin_unlock_bh(&tx->lock);
#endif

	uccp420wlan_prog_roc(ROC_START, pri_chnl_num, duration, type);

#ifdef MULTI_CHAN_SUPPORT
	if (uvif->chanctx)
		ieee80211_wake_queues(hw);
#endif

	mutex_unlock(&dev->mutex);

	return 0;
}


static int cancel_remain_on_channel(struct ieee80211_hw *hw)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)hw->priv;
	int ret = 0;

	mutex_lock(&dev->mutex);

	if (dev->roc_params.roc_in_progress) {
		dev->cancel_hw_roc_done = 0;
		dev->cancel_roc = 1;
		UCCP_DEBUG_ROC("%s:%d Cancelling HW ROC....\n",
				__func__, __LINE__);
		uccp420wlan_prog_roc(ROC_STOP, 0, 0, 0);

		mutex_unlock(&dev->mutex);

		if (!wait_for_cancel_hw_roc(dev)) {
			UCCP_DEBUG_ROC("%s:%d Cancel HW ROC....done\n",
							__func__,
							__LINE__);
			ret = 0;
		} else {
			UCCP_DEBUG_ROC("%s:%d Cancel HW ROC..timedout\n",
							__func__,
							__LINE__);
			ret = -1;
		}
	} else {
		mutex_unlock(&dev->mutex);
	}

	return ret;
}


/* Needed in case of IBSS to send out probe responses when we are beaconing */
static int tx_last_beacon(struct ieee80211_hw *hw)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)hw->priv;

	return dev->tx_last_beacon;
}


#ifdef CONFIG_PM
static int wait_for_econ_ps_cfg(struct mac80211_dev *dev)
{
	int count = 0;
	char econ_ps_cfg_done = 0;

check_econ_ps_cfg_complete:
	mutex_lock(&dev->mutex);
	econ_ps_cfg_done = dev->econ_ps_cfg_stats.completed;
	mutex_unlock(&dev->mutex);

	if (!econ_ps_cfg_done && (count < PS_ECON_CFG_TIMEOUT_TICKS)) {
		count++;
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);
		goto check_econ_ps_cfg_complete;
	}

	if (!econ_ps_cfg_done) {
		pr_warn("%s: Didn't get ECON_PS_CFG_DONE event\n",
		       __func__);
		return -1;
	}

	UCCP_DEBUG_80211IF("%s : Received ECON_PS_CFG_DONE event\n",
						__func__);
	return 0;
}

static int img_resume(struct ieee80211_hw *hw)
{
	int i = 0, ret = 0;
	int active_vif_index = -1;
	struct mac80211_dev *dev = NULL;

	if (hw == NULL) {
		pr_err("%s: Invalid parameters\n",
		       __func__);
		return -EINVAL;
	}

	dev = (struct mac80211_dev *)hw->priv;

	mutex_lock(&dev->mutex);

	for (i = 0; i < MAX_VIFS; i++) {
		if (dev->active_vifs & (1 << i))
			active_vif_index = i;
	}

	dev->econ_ps_cfg_stats.completed = 0;
	dev->econ_ps_cfg_stats.result = 0;

	ret = uccp420wlan_prog_econ_ps_state(active_vif_index,
					     PWRSAVE_STATE_AWAKE);
	if (ret) {
		pr_err("%s : prog econ ps failed\n",
		       __func__);
		mutex_unlock(&dev->mutex);
		return ret;
	}

	mutex_unlock(&dev->mutex);

	if (!wait_for_econ_ps_cfg(dev)) {
		if (!dev->econ_ps_cfg_stats.result) {
			UCCP_DEBUG_80211IF("%s: Successful\n",
				 __func__);
			hal_ops.disable_irq_wake();
			img_suspend_status = 0;
			return 0;
		}
		pr_warn("%s: Unable to Resume\n", __func__);
	}

	return -ETIME;
}


static int img_suspend(struct ieee80211_hw *hw,
		       struct cfg80211_wowlan *wowlan)
{
	int i = 0, ret = 0;
	int active_vif_index = -1;
	int count = 0;
	struct mac80211_dev *dev = NULL;
	struct ieee80211_vif *vif = NULL;

	if (hw == NULL) {
		pr_err("%s: Invalid parameters\n",
		       __func__);
		return -EINVAL;
	}

	if (WARN_ON((wifi->params.hw_scan_status == HW_SCAN_STATUS_PROGRESS)))
		return -EBUSY;

	dev = (struct mac80211_dev *)hw->priv;

	mutex_lock(&dev->mutex);

	for (i = 0; i < MAX_VIFS; i++) {
		if (dev->active_vifs & (1 << i)) {
			active_vif_index = i;
			count++;
		}
	}

	if (count != 1) {
		pr_err("%s: Economy mode supp only for single VIF(STA mode)\n",
		       __func__);
		mutex_unlock(&dev->mutex);
		return -ENOTSUPP;
	}

	rcu_read_lock();
	vif = rcu_dereference(dev->vifs[active_vif_index]);
	rcu_read_unlock();

	if (vif->type != NL80211_IFTYPE_STATION) {
		pr_err("%s: VIF is not in STA Mode\n",
		       __func__);
		mutex_unlock(&dev->mutex);
		return -ENOTSUPP;
	 }

	dev->econ_ps_cfg_stats.completed = 0;
	dev->econ_ps_cfg_stats.result = 0;
	dev->econ_ps_cfg_stats.wake_trig = -1;

	ret = uccp420wlan_prog_econ_ps_state(active_vif_index,
				PWRSAVE_STATE_DOZE);
	if (ret) {
		pr_err("%s : Error Occured\n",
		       __func__);
		mutex_unlock(&dev->mutex);

		return ret;
	}

	mutex_unlock(&dev->mutex);

	if (!wait_for_econ_ps_cfg(dev)) {
		if (!dev->econ_ps_cfg_stats.result) {
			UCCP_DEBUG_80211IF("%s: Successful\n",
				 __func__);
			hal_ops.enable_irq_wake();
			img_suspend_status = 1;
			return 0;
		}
		pr_info("%s: Unable to Suspend: Active Traffic.\n", __func__);
	}

	return -ETIME;
}
#endif


int scan(struct ieee80211_hw *hw,
	 struct ieee80211_vif *vif,
	 struct ieee80211_scan_request *ireq)
{
	struct umac_vif *uvif = (struct umac_vif *)vif->drv_priv;
	struct scan_req scan_req = {0};
	int i = 0;

	struct cfg80211_scan_request *req;

	req = &ireq->req;

	scan_req.n_ssids = req->n_ssids;
	scan_req.n_channels = req->n_channels;
	scan_req.ie_len = req->ie_len;

	if (wifi->params.hw_scan_status != HW_SCAN_STATUS_NONE)
		return -EBUSY; /* Already in HW SCAN State */

	/* Keep track of HW Scan requests and compeltes */
	wifi->params.hw_scan_status = HW_SCAN_STATUS_PROGRESS;

	if (uvif->dev->params->production_test == 1) {
		/* Drop scan, its just intended for IBSS
		 * and some data traffic
		 */
		if (wifi->params.hw_scan_status != HW_SCAN_STATUS_NONE) {
			ieee80211_scan_completed(uvif->dev->hw, false);
			wifi->params.hw_scan_status = HW_SCAN_STATUS_NONE;
		}

		return 0;
	}

	if (req->ie_len)
		memcpy(scan_req.ie, req->ie, req->ie_len);

	for (i = 0; i < req->n_channels; i++) {
		scan_req.center_freq[i] = req->channels[i]->center_freq;
		scan_req.freq_max_power[i] = req->channels[i]->max_power;
		scan_req.chan_flags[i] = req->channels[i]->flags;
		/* The type of scan comes from mac80211 so its taken care of */
	}

	scan_req.p2p_probe = req->no_cck;

	/* For hostapd scan (40MHz) and scan_type=passive, n_ssids=0
	 * and req->ssids is NULL
	 */
	if (req->n_ssids > 0) {
		for (i = 0; i < req->n_ssids; i++) {
			scan_req.ssids[i].ssid_len = req->ssids[i].ssid_len;
			if (req->ssids[i].ssid_len > 0)
				memcpy(scan_req.ssids[i].ssid,
				       req->ssids[i].ssid,
				       req->ssids[i].ssid_len);
		}
	}

	return uccp420wlan_scan(uvif->vif_index, &scan_req);
}


void uccp420wlan_scan_complete(void *context,
			       struct host_event_scanres *scan_res,
			       unsigned char *skb,
			       unsigned int len)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)context;

	/* DO NOT update the scan results through cfg80211 API's we just pass
	 * the beacons and probe responses up and mac80211 will inform cfg80211
	 */
	if (scan_res->more_results == 0) {
		UCCP_DEBUG_SCAN("Event Scan Complete from UCCP:");
		UCCP_DEBUG_SCAN(" More_results: 0, Scan is Completed\n");
		/* There can be a race where we receive remove_interface and
		 * abort the scan(1)
		 * But we get scan_complete from the FW(2), this check will make
		 * sure we are not calling scan_complete when we have already
		 * aborted the scan. Eg: Killing wpa_supplicant in middle of
		 * scanning
		 */
		if (wifi->params.hw_scan_status != HW_SCAN_STATUS_NONE) {
			dev->stats->umac_scan_complete++;
			ieee80211_scan_completed(dev->hw, false);

			/* Keep track of HW Scan requests and compeltes */
			wifi->params.hw_scan_status = HW_SCAN_STATUS_NONE;
		}
	} else {
		UCCP_DEBUG_SCAN("Event Scan Complete from UCCP:\n");
		UCCP_DEBUG_SCAN("More_results: %d, Still Scanning\n",
				scan_res->more_results);

	}
}


void cancel_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct umac_vif *uvif = (struct umac_vif *)vif->drv_priv;
	struct mac80211_dev *dev = NULL;

	dev = (struct mac80211_dev *)hw->priv;

	if (wifi->params.hw_scan_status == HW_SCAN_STATUS_PROGRESS) {
		pr_info("Aborting pending scan request...\n");

		dev->scan_abort_done = 0;

		if (uccp420wlan_scan_abort(uvif->vif_index))
			return;

		if (!wait_for_scan_abort(dev)) {
			ieee80211_scan_completed(hw, true);
			wifi->params.hw_scan_status = HW_SCAN_STATUS_NONE;
			dev->stats->umac_scan_complete++;
			return;
		}
	}
}


int set_rts_threshold(struct ieee80211_hw *hw,
		      u32 value)
{
	struct mac80211_dev *dev = NULL;

	dev = (struct mac80211_dev *)hw->priv;
	/*if thres>=2347 (default case) hostapd sends down (u32) -1*/
	if (value > 65536)
		dev->rts_threshold = 65536;
	else
		dev->rts_threshold = value;
	return 0;

}


int sta_add(struct ieee80211_hw *hw,
	    struct ieee80211_vif *vif,
	    struct ieee80211_sta *sta)
{
	struct umac_vif *uvif = (struct umac_vif *)vif->drv_priv;
	struct peer_sta_info peer_st_info = {0};
	int i;
	int result = 0;
	struct mac80211_dev *dev = hw->priv;
	struct umac_sta *usta = (struct umac_sta *)sta->drv_priv;
	unsigned int peer_id = 0;

	for (i = 0; i < MAX_PEERS; i++) {
		if (!dev->peers[i]) {
			peer_id = i;
			break;
		}
	}

	if (i == MAX_PEERS) {
		pr_err("Exceeded Max STA limit(%d)\n", MAX_PEERS);
		return -1;
	}


	for (i = 0; i < STA_NUM_BANDS; i++)
		peer_st_info.supp_rates[i] = sta->supp_rates[i];

	/* HT info */
	peer_st_info.ht_cap = sta->ht_cap.cap;
	peer_st_info.ht_supported = sta->ht_cap.ht_supported;
	peer_st_info.vht_supported = sta->vht_cap.vht_supported;
	peer_st_info.vht_cap = sta->vht_cap.cap;
	peer_st_info.ampdu_factor = sta->ht_cap.ampdu_factor;
	peer_st_info.ampdu_density = sta->ht_cap.ampdu_density;
	peer_st_info.rx_highest = sta->ht_cap.mcs.rx_highest;
	peer_st_info.tx_params = sta->ht_cap.mcs.tx_params;
	peer_st_info.uapsd_queues = sta->uapsd_queues;

	/* Will be used in enforcing rules during Aggregation */
	uvif->peer_ampdu_factor = (1 << (13 + sta->ht_cap.ampdu_factor)) - 1;

	if (sta->vht_cap.vht_supported) {
		if (sta->vht_cap.cap & IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE)
			uvif->dev->params->vht_beamform_support = 1;

	}

	for (i = 0; i < HT_MCS_MASK_LEN; i++)
		peer_st_info.rx_mask[i] = sta->ht_cap.mcs.rx_mask[i];

	for (i = 0; i < ETH_ALEN; i++)
		peer_st_info.addr[i] = sta->addr[i];

	result = uccp420wlan_sta_add(uvif->vif_index, &peer_st_info);

	if (!result) {
		rcu_assign_pointer(dev->peers[peer_id], sta);
		synchronize_rcu();

		usta->index = peer_id;
#ifdef MULTI_CHAN_SUPPORT
		usta->chanctx = uvif->chanctx;
		usta->vif_index = uvif->vif_index;
#endif
	}

	return result;
}


int sta_remove(struct ieee80211_hw *hw,
	       struct ieee80211_vif *vif,
	       struct ieee80211_sta *sta)
{
	struct umac_vif *uvif = (struct umac_vif *)vif->drv_priv;
	struct peer_sta_info peer_st_info = {0};
	int i;
	int result = 0;
	struct mac80211_dev *dev = hw->priv;
	struct umac_sta *usta = (struct umac_sta *)sta->drv_priv;

	for (i = 0; i < ETH_ALEN; i++)
		peer_st_info.addr[i] = sta->addr[i];

	result = uccp420wlan_sta_remove(uvif->vif_index, &peer_st_info);

	if (!result) {
		rcu_assign_pointer(dev->peers[usta->index], NULL);
		synchronize_rcu();

		usta->index = -1;
	}

	return result;
}


static int load_fw(struct ieee80211_hw *hw)
{
	int err = 0;
	int i = 0;
	struct mac80211_dev *dev = (struct mac80211_dev *)hw->priv;
	const struct firmware *fw = NULL;
	const char *bin_name[FWLDR_NUM_BINS] = {FWLDR_HW_BIN,
						FWLDR_FW_BIN};

	do {
		err = request_firmware(&fw, bin_name[i], dev->dev);

		if (err) {
			pr_err("Failed to get %s, Error = %d\n",
			       bin_name[i],
			       err);
			break;
		}

		err = fwldr_load_fw(fw->data, i);

		if (err == FWLDR_SUCCESS)
			pr_info("%s is loaded\n", bin_name[i]);
		else
			pr_err("Loading of %s failed\n", bin_name[i]);

		release_firmware(fw);

		i++;

	} while ((i < FWLDR_NUM_BINS) && (!err));

	return err;
}

static void channel_switch_beacon(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct cfg80211_chan_def *chandef)
{
	pr_err("RECEIVED CHANNEL SWITCH BEACON\n");
}

#ifdef DFS_TEST
static void radar_detected(void)
{
	ieee80211_radar_detected(wifi->hw);
}
#endif

#ifdef CONFIG_NL80211_TESTMODE
const struct nla_policy rpu_testmode_policy[RPU_TM_ATTR_MAX + 1] = {
	[RPU_TM_ATTR_CMD] = { .type = NLA_U32 },
	[RPU_TM_ATTR_DUMP] = { .type = NLA_UNSPEC },
};

#define CB_ARG_OFFSET_ID 3
#define CB_ARG_OFFSET_DUMP_START 4
#define CB_ARG_OFFSET_DUMP_LEN 5
/*Assuming minium dump of MAX_NL_DUMP_LEN*/
/* Control Buffer is used as below
 * cb[3] ==> To identify First Command
 * cb[4] ==> Storing DUMP Start
 * cb[5] ==> Dump Len
 */

static int rpu_testmode_dump(struct ieee80211_hw *hw, struct sk_buff *skb,
			struct netlink_callback *cb,  void *data, int len)
{

	int idx = 0;
	int err;
	int cmd = 0;
	long dump_start = 0;
	char *curr_dump;
	unsigned long dump_len = cb->args[CB_ARG_OFFSET_DUMP_LEN],
				 curr_msg_len = MAX_NL_DUMP_LEN;
	unsigned long no_of_msgs = dump_len/MAX_NL_DUMP_LEN;
	struct nlattr *tb[RPU_TM_ATTR_MAX + 1];
	struct mac80211_dev *dev = (struct mac80211_dev *)hw->priv;

	mutex_lock(&dev->mutex);
	idx = cb->args[CB_ARG_OFFSET_ID];
	if (cb->args[CB_ARG_OFFSET_DUMP_START])
		dump_start = cb->args[CB_ARG_OFFSET_DUMP_START];

	/*MAX Message: Dump Over*/
	if (idx > no_of_msgs) {
		if (dump_start) {
			kfree((void *)dump_start);
			cb->args[CB_ARG_OFFSET_DUMP_START] = 0;
			dump_start = 0;
		}
		goto dump_fail;
	}

	/*Get Dump only once per command*/
	if (!idx) {
		err = nla_parse(tb, RPU_TM_ATTR_MAX, data,
					 len, rpu_testmode_policy);
		if (err)
			goto dump_fail;
		if (!tb[RPU_TM_ATTR_CMD]) {
			pr_err("%s: CMD Attribute not found\n", __func__);
			goto dump_fail;
		}

		cmd = nla_get_u32(tb[RPU_TM_ATTR_CMD]);

		switch (cmd) {
		case RPU_TM_CMD_GRAM:
			if (hal_ops.get_dump_gram(&dump_start))
				goto dump_fail;
		break;
		case RPU_TM_CMD_COREA:
			if (hal_ops.get_dump_core(&dump_start, 0))
				goto dump_fail;
		break;
		case RPU_TM_CMD_COREB:
			if (hal_ops.get_dump_core(&dump_start, 1))
				goto dump_fail;
		break;
		case RPU_TM_CMD_PERIP:
			if (hal_ops.get_dump_perip(&dump_start))
				goto dump_fail;
		break;
		case RPU_TM_CMD_SYSBUS:
			if (hal_ops.get_dump_sysbus(&dump_start))
				goto dump_fail;
		break;
		default:
			pr_err("%s: no match\n", __func__);
		}

		dump_len = hal_ops.get_dump_len(cmd);
		cb->args[CB_ARG_OFFSET_DUMP_START] = dump_start;
		cb->args[CB_ARG_OFFSET_DUMP_LEN] = dump_len;
		no_of_msgs = dump_len/MAX_NL_DUMP_LEN;
	}

	/*Last Message of the Dump*/
	if (idx == no_of_msgs)
		curr_msg_len = (dump_len % MAX_NL_DUMP_LEN);

	curr_dump = ((char *)dump_start) + (MAX_NL_DUMP_LEN * idx);

	if (!curr_dump  || (curr_dump < (char *)dump_start) ||
		(curr_dump > ((char *)dump_start + dump_len)))
			goto dump_fail;

	if (curr_msg_len > skb_tailroom(skb))
		goto dump_fail;

	err = nla_put(skb, RPU_TM_ATTR_DUMP, curr_msg_len, curr_dump);
	if (err)
		goto dump_fail;
	cb->args[CB_ARG_OFFSET_ID] = ++idx;

	mutex_unlock(&dev->mutex);
	return 0;

dump_fail:
	mutex_unlock(&dev->mutex);
	return -ENOBUFS;
}
#endif

#ifdef MULTI_CHAN_SUPPORT
static int umac_chanctx_set_channel(struct mac80211_dev *dev,
				     struct umac_vif *uvif,
				     struct cfg80211_chan_def *chandef)
{
	unsigned int freq_band = 0;
	unsigned int ch_width = 0;
	int center_freq1 = 0;
	int center_freq2 = 0;
	unsigned int pri_chan;
	int err = 0;

	pri_chan = ieee80211_frequency_to_channel(chandef->chan->center_freq);
	center_freq1 = chandef->center_freq1;
	center_freq2 = chandef->center_freq2;

	freq_band = chandef->chan->band;
	ch_width = chandef->width;
	DEBUG_LOG("%s: Primary Channel is: %d\n", __func__, pri_chan);
	err = uccp420wlan_prog_channel(pri_chan, center_freq1,
				       center_freq2,
				       ch_width,
				       uvif->vif_index,
				       freq_band);

	if (!err) {
		/* RPU expects to program the associated channel
		 * every time it changes, else it leads to
		 * disconnections.
		 */
		uccp420wlan_prog_vif_op_channel(uvif->vif_index,
						uvif->vif->addr,
						pri_chan);
	}

	return err;
}


static int add_chanctx(struct ieee80211_hw *hw,
		       struct ieee80211_chanctx_conf *conf)
{
	struct mac80211_dev *dev = NULL;
	struct umac_chanctx *ctx = NULL;
	int chanctx_id = 0;
	int i = 0;

	dev = hw->priv;

	UCCP_DEBUG_TSMC("GOT add chanctx\n");

	for (i = 0; i < MAX_CHANCTX; i++) {
		if (!dev->chanctx[i]) {
			chanctx_id = i;
			break;
		}
	}

	if (i == MAX_CHANCTX) {
		pr_err("Exceeded Max chan contexts limit(%d)\n", MAX_CHANCTX);
		return -1;
	}

	UCCP_DEBUG_TSMC("%s: %d MHz\n",
			__func__,
			conf->def.chan->center_freq);

	mutex_lock(&dev->mutex);

	ctx = (struct umac_chanctx *)conf->drv_priv;
	ctx->index = chanctx_id;
	INIT_LIST_HEAD(&ctx->vifs);
	ctx->nvifs = 0;

	rcu_assign_pointer(dev->chanctx[i], conf);
	synchronize_rcu();

	mutex_unlock(&dev->mutex);
	return 0;
}


static void remove_chanctx(struct ieee80211_hw *hw,
			   struct ieee80211_chanctx_conf *conf)
{
	struct mac80211_dev *dev = NULL;
	struct umac_chanctx *ctx = NULL;

	dev = hw->priv;
	ctx = (struct umac_chanctx *)conf->drv_priv;
	UCCP_DEBUG_TSMC("GOT remove chanctx\n");

	UCCP_DEBUG_TSMC("%s: %d MHz\n",
					 __func__,
					 conf->def.chan->center_freq);

	mutex_lock(&dev->mutex);

	/* Unassign_vif_chanctx should have been called to free all the assigned
	 * vifs before this call is called, hence we dont need to specifically
	 * free the vifs here
	 */
	rcu_assign_pointer(dev->chanctx[ctx->index], NULL);
	synchronize_rcu();

	ctx->index = -1;

	mutex_unlock(&dev->mutex);
}


static void change_chanctx(struct ieee80211_hw *hw,
			   struct ieee80211_chanctx_conf *conf,
			   u32 changed)
{
	struct umac_vif *uvif = NULL;
	struct mac80211_dev *dev = NULL;
	struct umac_chanctx *ctx = NULL;
	int i = 0;
	int center_freq = 0;
	int chan = 0;
	int err = 0;

	dev = hw->priv;
	ctx = (struct umac_chanctx *)conf->drv_priv;

	UCCP_DEBUG_TSMC("Got change_chanctx: %d\n", changed);
	pr_err("%s: %d MHz\n", __func__, conf->def.chan->center_freq);

	if (changed & IEEE80211_CHANCTX_CHANGE_WIDTH ||
	    changed & IEEE80211_CHANCTX_CHANGE_CHANNEL) {
		pr_err("%s channel width = %d channel = %d\n", __func__,
				conf->def.width, conf->def.center_freq1);

		center_freq = conf->def.chan->center_freq;
		chan = ieee80211_frequency_to_channel(center_freq);

		list_for_each_entry(uvif, &ctx->vifs, list) {
			err = umac_chanctx_set_channel(dev, uvif, &conf->def);

			if (err) {
				pr_err("%s: Failed to set channel/width\n",
				       __func__);
				return;
			}
		}
	}

	if (changed & IEEE80211_CHANCTX_CHANGE_MIN_WIDTH) {
		UCCP_DEBUG_TSMC("%s Minimum channel width = %d\n", __func__,
			conf->min_def.width);

		center_freq = conf->min_def.chan->center_freq;
		chan = ieee80211_frequency_to_channel(center_freq);

		list_for_each_entry(uvif, &ctx->vifs, list) {
			err = umac_chanctx_set_channel(dev, uvif,
						       &conf->min_def);

			if (err) {
				pr_err("%s: Failed to set channel/width\n",
					__func__);

				return;
			}
		}
	}

	/* TODO: Make this global config as it effects all VIF's */
	if (changed & IEEE80211_CHANCTX_CHANGE_RX_CHAINS) {
		UCCP_DEBUG_TSMC("%s rx_chains_static=%d rx_chains_dynamic=%d\n",
			__func__, conf->rx_chains_static,
			conf->rx_chains_dynamic);

		list_for_each_entry(uvif, &ctx->vifs, list) {
			for (i = 0; i < MAX_VIFS; i++) {
				if (!(dev->active_vifs & (1 << i)))
					continue;

				if (dev->vifs[i] &&
				    !ether_addr_equal(dev->vifs[i]->addr,
						      uvif->vif->addr))
					continue;

				if (conf->rx_chains_static > 1)
					uccp420wlan_prog_vif_smps(i,
						uvif->vif->addr,
						IEEE80211_SMPS_OFF);
				else if (conf->rx_chains_dynamic > 1)
					uccp420wlan_prog_vif_smps(i,
						uvif->vif->addr,
						IEEE80211_SMPS_DYNAMIC);
				else
					uccp420wlan_prog_vif_smps(i,
						uvif->vif->addr,
						IEEE80211_SMPS_STATIC);
			}
		}
	}
	if (changed & IEEE80211_CHANCTX_CHANGE_RADAR) {

		UCCP_DEBUG_80211IF("%s radar enabled =%d\n",
				   __func__,
				   conf->radar_enabled);

		if (conf->radar_enabled)
			uccp420wlan_prog_radar_detect(RADAR_DETECT_OP_START);
		else
			uccp420wlan_prog_radar_detect(RADAR_DETECT_OP_STOP);
	}
}


static int assign_vif_chanctx(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_chanctx_conf *conf)
{
	struct mac80211_dev *dev = NULL;
	struct umac_vif *uvif = NULL;
	struct umac_chanctx *ctx = NULL;
	int prog_chanctx_time_info = 0;
	int err = 0;

	dev = hw->priv;
	uvif = (struct umac_vif *)vif->drv_priv;
	ctx = (struct umac_chanctx *)conf->drv_priv;
	UCCP_DEBUG_TSMC("Got assign_vif_chanctx\n");

	DEBUG_LOG("%s: addr: %pM, type: %d, p2p: %d chan: %d MHz\n",
		  __func__,
		  vif->addr,
		  vif->type,
		  vif->p2p,
		  conf->def.chan->center_freq);

	mutex_lock(&dev->mutex);

	uvif->chanctx = ctx;
	list_add_tail(&uvif->list, &ctx->vifs);

	prog_chanctx_time_info = !(ctx->nvifs);
	ctx->nvifs++;

	/* If this is the first vif being assigned to the channel context then
	 * increment our count of the active channel contexts
	 */
	if (prog_chanctx_time_info) {
		if (!dev->num_active_chanctx)
			dev->curr_chanctx_idx = ctx->index;

		dev->num_active_chanctx++;
		uccp420wlan_prog_chanctx_time_info();
	}

	err = umac_chanctx_set_channel(dev, uvif, &conf->def);

	mutex_unlock(&dev->mutex);

	return err;
}


static void unassign_vif_chanctx(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_chanctx_conf *conf)
{
	struct mac80211_dev *dev = NULL;
	struct umac_vif *uvif = NULL;
	struct umac_chanctx *ctx = NULL;
	u32 hw_queue_map = 0;
	int i = 0;

	dev = hw->priv;
	uvif = (struct umac_vif *)vif->drv_priv;
	ctx = (struct umac_chanctx *)conf->drv_priv;

	UCCP_DEBUG_TSMC("Got unassign_vif_chanctx\n");

	DEBUG_LOG("%s: addr: %pM, type: %d, p2p: %d chan: %d MHz\n",
		  __func__,
		  vif->addr,
		  vif->type,
		  vif->p2p,
		  conf->def.chan->center_freq);

	mutex_lock(&dev->mutex);

	/* We need to specifically handle flushing tx queues for the AP VIF
	 * here (for STA VIF, mac80211 handles this via flush_queues)
	 */
	if (vif->type == NL80211_IFTYPE_AP) {
		/* Flush all queues for this VIF */
		for (i = 0; i < NUM_ACS; i++)
			hw_queue_map |= BIT(i);

		uccp420_flush_vif_queues(dev,
					 uvif,
					 uvif->chanctx->index,
					 hw_queue_map,
					 UMAC_VIF_CHANCTX_TYPE_OPER);
	}

	uvif->chanctx = NULL;

	list_del(&uvif->list);
	ctx->nvifs--;

	if (!ctx->nvifs) {
		dev->num_active_chanctx--;

		if (dev->num_active_chanctx)
			uccp420wlan_prog_chanctx_time_info();
	}

	mutex_unlock(&dev->mutex);
}


static int switch_vif_chanctx(struct ieee80211_hw *hw,
			      struct ieee80211_vif_chanctx_switch *vifs,
			      int n_vifs,
			      enum ieee80211_chanctx_switch_mode mode)
{
	struct mac80211_dev *dev = NULL;
	int ret = 0;

	UCCP_DEBUG_TSMC("Got switch_vif_chanctx\n");
	dev = hw->priv;

	/*TODO*/
	if (n_vifs > 1)
		return -EOPNOTSUPP;

	pr_err("%s switch_vif_chanctx switch freq %hu->%hu width %d->%d\n",
		 __func__,
		vifs[0].old_ctx->def.chan->center_freq,
		vifs[0].new_ctx->def.chan->center_freq,
		vifs[0].old_ctx->def.width,
		vifs[0].new_ctx->def.width);

	switch (mode) {
	case CHANCTX_SWMODE_SWAP_CONTEXTS:
		unassign_vif_chanctx(hw, vifs[0].vif, vifs[0].old_ctx);
		remove_chanctx(hw, vifs[0].old_ctx);
		add_chanctx(hw, vifs[0].new_ctx);
		assign_vif_chanctx(hw, vifs[0].vif, vifs[0].new_ctx);
		break;
	case CHANCTX_SWMODE_REASSIGN_VIF:
		unassign_vif_chanctx(hw, vifs[0].vif, vifs[0].old_ctx);
		assign_vif_chanctx(hw, vifs[0].vif, vifs[0].new_ctx);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	return ret;
}


static void flush_queues(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 u32 queues,
			 bool drop)
{
	struct mac80211_dev *dev = NULL;
	struct umac_vif *uvif = NULL;
	u32 hw_queue_map = 0;
	int i = 0;

	dev = hw->priv;

	mutex_lock(&dev->mutex);

	if (!vif)
		goto out;

	uvif = (struct umac_vif *)vif->drv_priv;

	if (!uvif->chanctx)
		goto out;

	/* Convert the mac80211 queue map to our hw queue map */
	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		if (queues & BIT(i))
			hw_queue_map |= BIT(tx_queue_map(i));
	}
	/* This op should not get called during ROC operation, so we can assume
	 * that the vif_chanctx_type will be UMAC_VIF_CHANCTX_TYPE_OPER. As for
	 * TSMC operation the VIF can only be associated to one channel context,
	 * so we pass uvif->chanctx->index as the parameter for chanctx_idx
	 */
	uccp420_flush_vif_queues(dev,
				 uvif,
				 uvif->chanctx->index,
				 hw_queue_map,
				 UMAC_VIF_CHANCTX_TYPE_OPER);

out:
	mutex_unlock(&dev->mutex);
}
#endif


static struct ieee80211_ops ops = {
	.tx                 = tx,
	.start              = start,
	.stop               = stop,
	.add_interface      = add_interface,
	.remove_interface   = remove_interface,
	.config             = config,
	.prepare_multicast  = prepare_multicast,
	.configure_filter   = configure_filter,
	.sw_scan_start      = NULL,
	.sw_scan_complete   = NULL,
	.get_stats          = NULL,
	.sta_notify         = NULL,
	.conf_tx            = conf_vif_tx,
	.bss_info_changed   = bss_info_changed,
	.set_tim            = NULL,
	.set_key            = set_key,
	.tx_last_beacon     = tx_last_beacon,
	.ampdu_action       = ampdu_action,
	.set_antenna	    = set_antenna,
	.remain_on_channel = remain_on_channel,
	.cancel_remain_on_channel = cancel_remain_on_channel,
#ifdef CONFIG_PM
	.suspend	    = img_suspend,
	.resume		    = img_resume,
#endif
	.hw_scan	    = scan,
	.cancel_hw_scan	    = cancel_hw_scan,
	.set_rekey_data     = NULL,
	.set_rts_threshold  = set_rts_threshold,
	.sta_add	    = sta_add,
	.sta_remove	    = sta_remove,
	.channel_switch_beacon = channel_switch_beacon,
	CFG80211_TESTMODE_DUMP(rpu_testmode_dump)
#ifdef MULTI_CHAN_SUPPORT
	.add_chanctx              = add_chanctx,
	.remove_chanctx           = remove_chanctx,
	.change_chanctx           = change_chanctx,
	.assign_vif_chanctx       = assign_vif_chanctx,
	.unassign_vif_chanctx     = unassign_vif_chanctx,
	.switch_vif_chanctx       = switch_vif_chanctx,
	.flush			  = flush_queues,
#endif
};

static void uccp420wlan_exit(void)
{
	/* DEV Release */
	struct mac80211_dev *dev = (struct mac80211_dev *)wifi->hw->priv;

	if (wifi->hw) {
		ieee80211_unregister_hw(wifi->hw);
		device_release_driver(dev->dev);
		device_destroy(hwsim_class, 0);
		ieee80211_free_hw(wifi->hw);
		wifi->hw = NULL;
	}

	class_destroy(hwsim_class);
}

static int uccp420wlan_init(void)
{
	struct ieee80211_hw *hw;
	int error;
	struct mac80211_dev *dev = NULL;
	int i;

	/* Allocate new hardware device */
	hw = ieee80211_alloc_hw(sizeof(struct mac80211_dev), &ops);

	if (hw == NULL) {
		pr_err("Failed to allocate memory for ieee80211_hw\n");
		error = -ENOMEM;
		goto out;
	}

	dev = (struct mac80211_dev *)hw->priv;
	memset(dev, 0, sizeof(struct mac80211_dev));

	hwsim_class = class_create(THIS_MODULE, "uccp420");

	if (IS_ERR(hwsim_class)) {
		pr_err("Failed to create the device class\n");
		error = PTR_ERR(hwsim_class);
		goto out;
	}

	/* Only 1 per physical intf*/
	dev->dev = device_create(hwsim_class, NULL, 0, hw, "uccwlan");

	if (IS_ERR(dev->dev)) {
		pr_err("uccwlan: device_create failed (%ld)\n",
		       PTR_ERR(dev->dev));
		error = -ENOMEM;
		goto auto_dev_class_failed;
	}

	dev->dev->driver = &img_uccp_driver.driver;

	if (device_is_registered(dev->dev)) {
		error = device_bind_driver(dev->dev);
	} else {
		pr_err("Device is not registered\n");
		error = -ENODEV;
		goto failed_hw;
	}

	if (error != 0) {
		pr_err("uccwlan: device_bind_driver failed (%d)\n", error);
		goto failed_hw;
	}

	pr_info("MAC ADDR: %pM\n", vif_macs);
	SET_IEEE80211_DEV(hw, dev->dev);

	mutex_init(&dev->mutex);
	spin_lock_init(&dev->bcast_lock);
#ifdef MULTI_CHAN_SUPPORT
	spin_lock_init(&dev->chanctx_lock);
#endif

	spin_lock_init(&dev->roc_lock);
	dev->state = STOPPED;
	dev->active_vifs = 0;
	dev->txpower = DEFAULT_TX_POWER;
	dev->tx_antenna = DEFAULT_TX_ANT_SELECT;
	dev->rts_threshold = DEFAULT_RTS_THRESHOLD;
	strncpy(dev->name, UCCP_DRIVER_NAME, 11);
	dev->name[11] = '\0';

	for (i = 0; i < wifi->params.num_vifs; i++)
		ether_addr_copy(dev->if_mac_addresses[i].addr, vif_macs[i]);

	/* Initialize HW parameters */
	init_hw(hw);
	dev->hw = hw;
	dev->params = &wifi->params;
	dev->stats = &wifi->stats;
	dev->umac_proc_dir_entry = wifi->umac_proc_dir_entry;
	dev->current_vif_count = 0;
	dev->stats->system_rev = system_rev;
#ifdef MULTI_CHAN_SUPPORT
	dev->num_active_chanctx = 0;

	for (i = 0; i < MAX_VIFS; i++)
		dev->vifs[i] = NULL;
#endif

	/*Register hardware*/
	error = ieee80211_register_hw(hw);

	/* Production test hack: Set all channel flags to 0 to allow IBSS
	 * creation in all channels
	 */
	if (wifi->params.production_test && !error) {
		enum ieee80211_band band;
		struct ieee80211_supported_band *sband;

		for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
			sband = hw->wiphy->bands[band];
			if (sband)
				for (i = 0; i < sband->n_channels; i++)
					sband->channels[i].flags = 0;
		}
	}

	if (!error) {
		wifi->hw = hw;
		goto out;
	} else {
		uccp420wlan_exit();
		goto out;
	}

failed_hw:
	device_release_driver(dev->dev);
	device_destroy(hwsim_class, 0);
auto_dev_class_failed:
	class_destroy(hwsim_class);
out:
	return error;
}


static char *uccp420_get_vif_name(int vif_idx)
{
	struct mac80211_dev *dev = ((struct mac80211_dev *)(wifi->hw->priv));
	struct wireless_dev *wdev = NULL;
	struct ieee80211_vif *vif = NULL;

	if ((dev->active_vifs & (1 << vif_idx))) {
		rcu_read_lock();
		vif = rcu_dereference(dev->vifs[vif_idx]);
		rcu_read_unlock();

		wdev = ieee80211_vif_to_wdev(vif);
		return wdev->netdev->name;
	}

	return NULL;
}


static int proc_read_config(struct seq_file *m, void *v)
{
	int i = 0;
	int cnt = 0;
	int rf_params_size = sizeof(wifi->params.rf_params) /
			     sizeof(wifi->params.rf_params[0]);
	struct mac80211_dev *dev = ((struct mac80211_dev *)(wifi->hw->priv));

	seq_puts(m, "************* Configurable Parameters ***********\n");
	seq_printf(m, "dot11g_support = %d\n", wifi->params.dot11g_support);
	seq_printf(m, "dot11a_support = %d\n", wifi->params.dot11a_support);
	seq_printf(m, "sensitivity = %d\n", wifi->params.ed_sensitivity);
	seq_printf(m, "auto_sensitivity = %d\n", wifi->params.auto_sensitivity);
	/*RF Input params*/
	seq_puts(m, "rf_params =");
	for (i = 0; i < rf_params_size; i++)
		seq_printf(m, " %02X", wifi->params.rf_params[i]);

	seq_puts(m, "\n");

	seq_puts(m, "rf_params_vpd =");
	for (i = 0; i < rf_params_size; i++)
		seq_printf(m, " %02X", wifi->params.rf_params_vpd[i]);

	seq_puts(m, "\n");

	seq_printf(m, "production_test = %d\n", wifi->params.production_test);
	seq_printf(m, "bypass_vpd = %d\n", wifi->params.bypass_vpd);
	seq_printf(m, "tx_fixed_mcs_indx = %d (%s)\n",
		   wifi->params.tx_fixed_mcs_indx,
		   (wifi->params.prod_mode_rate_flag &
		    ENABLE_VHT_FORMAT) ?
		   "VHT" : (wifi->params.prod_mode_rate_flag &
			    ENABLE_11N_FORMAT) ? "HT" : "Not Set");
	if (wifi->params.tx_fixed_rate > -1) {
		if (wifi->params.tx_fixed_rate == 55)
			seq_puts(m, "tx_fixed_rate = 5.5\n");
		else
			seq_printf(m, "tx_fixed_rate = %d\n",
				   wifi->params.tx_fixed_rate);
	} else
		seq_printf(m, "tx_fixed_rate = %d\n",
			   wifi->params.tx_fixed_rate);
	seq_printf(m, "num_spatial_streams (Per Frame) = %d\n",
		   wifi->params.num_spatial_streams);
	seq_printf(m, "uccp_num_spatial_streams (UCCP Init) = %d\n",
		   wifi->params.uccp_num_spatial_streams);
	seq_printf(m, "antenna_sel (UCCP Init) = %d\n",
		   wifi->params.antenna_sel);
	seq_printf(m, "max_data_size = %d (%dK)\n",
		   wifi->params.max_data_size,
		   wifi->params.max_data_size/1024);
	seq_printf(m, "max_tx_cmds = %d\n",
		   wifi->params.max_tx_cmds);
	seq_printf(m, "disable_power_save (Disables all power save's) = %d\n",
		   wifi->params.disable_power_save);
	seq_printf(m, "disable_sm_power_save (Disables MIMO PS only) = %d\n",
		   wifi->params.disable_sm_power_save);
	seq_printf(m, "mgd_mode_tx_fixed_mcs_indx = %d (%s)\n",
		   wifi->params.mgd_mode_tx_fixed_mcs_indx,
		   (wifi->params.prod_mode_rate_flag &
		    ENABLE_VHT_FORMAT) ?
		   "VHT" : (wifi->params.prod_mode_rate_flag &
			    ENABLE_11N_FORMAT) ? "HT" : "Not Set");
	if (wifi->params.mgd_mode_tx_fixed_rate > -1) {
		if (wifi->params.mgd_mode_tx_fixed_rate == 55)
			seq_puts(m, "mgd_mode_tx_fixed_rate = 5.5\n");
		else
			seq_printf(m, "mgd_mode_tx_fixed_rate = %d\n",
				   wifi->params.mgd_mode_tx_fixed_rate);
	} else
		seq_printf(m, "mgd_mode_tx_fixed_rate = %d\n",
			   wifi->params.mgd_mode_tx_fixed_rate);

	seq_printf(m, "num_vifs = %d\n",
		   wifi->params.num_vifs);

	seq_printf(m, "chnl_bw = %d\n",
		   wifi->params.chnl_bw);

	seq_printf(m, "prod_mode_chnl_bw_40_mhz = %d\n",
		   wifi->params.prod_mode_chnl_bw_40_mhz);
	if (vht_support)
		seq_printf(m, "prod_mode_chnl_bw_80_mhz = %d\n",
			   wifi->params.prod_mode_chnl_bw_80_mhz);
	seq_printf(m, "sec_ch_offset_40_plus = %d\n",
		   wifi->params.sec_ch_offset_40_plus);
	seq_printf(m, "sec_ch_offset_40_minus = %d\n",
		   wifi->params.sec_ch_offset_40_minus);

	if (vht_support) {
		seq_printf(m, "sec_40_ch_offset_80_plus = %d\n",
			   wifi->params.sec_40_ch_offset_80_plus);
		seq_printf(m, "sec_40_ch_offset_80_minus = %d\n",
			   wifi->params.sec_40_ch_offset_80_minus);
	}
	seq_printf(m, "rate_protection_type = %d (0: Disable, 1: Enable)\n",
		   wifi->params.rate_protection_type);
	seq_puts(m, "Bits:80MHz-VHT-11N-SGI-40MHz-GF\n");
	seq_printf(m, "prod_mode_rate_flag = %d\n",
		   wifi->params.prod_mode_rate_flag);
	seq_printf(m, "prod_mode_rate_preamble_type (0: Short, 1: Long) = %d\n",
		   wifi->params.prod_mode_rate_preamble_type);
	seq_printf(m, "prod_mode_stbc_enabled = %d\n",
		   wifi->params.prod_mode_stbc_enabled);
	seq_printf(m, "prod_mode_bcc_or_ldpc = %d\n",
		   wifi->params.prod_mode_bcc_or_ldpc);
	seq_printf(m, "vht_beamformer_enable = %d\n",
		   wifi->params.vht_beamform_enable);
	seq_printf(m, "vht_beamformer_period = %dms\n",
		   wifi->params.vht_beamform_period);
	seq_printf(m, "bg_scan_enable = %d\n",
		   wifi->params.bg_scan_enable);
	seq_puts(m, "bg_scan_channel_list =");

	for (i = 0; i < wifi->params.bg_scan_num_channels;  i++) {
		if (wifi->params.bg_scan_channel_list[i])
			seq_printf(m, " %d",
				   wifi->params.bg_scan_channel_list[i]);
	}

	seq_puts(m, "\n");
	seq_puts(m, "bg_scan_channel_flags =");

	for (i = 0; i < wifi->params.bg_scan_num_channels;  i++) {
		if (wifi->params.bg_scan_channel_flags[i])
			seq_printf(m, " %d",
				   wifi->params.bg_scan_channel_flags[i]);
	}

	seq_puts(m, "\n");
	seq_printf(m, "bg_scan_intval = %dms\n",
		   wifi->params.bg_scan_intval/1000);

	/*currently not used in LMAC, so don't export to user*/
#if 0
	seq_printf(m, "bg_scan_chan_dur = %d\n", wifi->params.bg_scan_chan_dur);
	seq_printf(m, "bg_scan_serv_chan_dur = %d\n",
		   wifi->params.bg_scan_serv_chan_dur);
#endif
	seq_printf(m, "bg_scan_num_channels = %d\n",
		   wifi->params.bg_scan_num_channels);
	seq_printf(m, "nw_selection = %d\n",
		   wifi->params.nw_selection);
	seq_printf(m, "scan_type = %d (PASSIVE: 0, ACTIVE: 1)\n",
		   wifi->params.scan_type);
#ifdef PERF_PROFILING
	seq_printf(m, "driver_tput = %d\n",
		   wifi->params.driver_tput);
#endif
	seq_printf(m, "fw_loading = %d\n", wifi->params.fw_loading);
	seq_printf(m, "bt_state = %d\n", wifi->params.bt_state);

	/* Beacon Time Stamp */
	if (dev->state == STARTED) {
		for (cnt = 0; cnt < MAX_VIFS; cnt++) {
			unsigned long long ts1;
			unsigned long long bssid, atu;
			int status;
			unsigned int t2;

			spin_lock_bh(&tsf_lock);
			ts1 = get_unaligned_le64(wifi->params.sync[cnt].ts1);
			bssid =
			    get_unaligned_le64(wifi->params.sync[cnt].bssid);
			status = wifi->params.sync[cnt].status;
			atu = wifi->params.sync[cnt].atu;
			t2 = wifi->params.sync[cnt].ts2;
			spin_unlock_bh(&tsf_lock);
			if (status)
				seq_printf(m,
					   "sync=%s %d %llu %llu %llx t2=%u\n",
					   uccp420_get_vif_name(cnt),
					   status,
					   (unsigned long long)ts1,
					   atu,
					   (unsigned long long)bssid,
					   t2);
		}
	}

	seq_puts(m, "****** Production Test (or) FTM Parameters *******\n");
	seq_printf(m, "start_packet_gen = %d (-1: Infinite loop)\n",
		   wifi->params.pkt_gen_val);
	seq_printf(m, "payload_length = %d bytes\n",
		   wifi->params.payload_length);
	seq_printf(m, "start_prod_mode = channel: %d\n",
		   wifi->params.start_prod_mode);
	seq_printf(m, "continuous_tx = %d\n",
		   wifi->params.cont_tx);

	if (ftm || wifi->params.production_test)
		seq_printf(m, "set_tx_power = %d dB\n",
			   wifi->params.set_tx_power);

	seq_printf(m, "center_frequency = %d\n",
		   ieee80211_frequency_to_channel(dev->cur_chan.center_freq1));

	if (ftm)
		seq_printf(m, "aux_adc_chain_id = %d\n",
			   wifi->params.aux_adc_chain_id);

	seq_puts(m, "UCCP Runtime Debug Support Configuration.\n");
	seq_printf(m, "uccp_debug = %d.\n", uccp_debug);
	if (uccp_debug == UCCP_DBG_DEFAULT)
		seq_puts(m, "***uccp_debug: All debugs are disabled.\n");
	if (uccp_debug & UCCP_DEBUG_SCAN)
		seq_puts(m, "***uccp_debug: UCCP_DEBUG_SCAN\n");
	if (uccp_debug & UCCP_DEBUG_ROC)
		seq_puts(m, "***uccp_debug: UCCP_DEBUG_ROC\n");
	if (uccp_debug & UCCP_DEBUG_TX)
		seq_puts(m, "***uccp_debug: UCCP_DEBUG_TX\n");
	if (uccp_debug & UCCP_DEBUG_CORE)
		seq_puts(m, "***uccp_debug: UCCP_DEBUG_CORE\n");
	if (uccp_debug & UCCP_DEBUG_IF)
		seq_puts(m, "***uccp_debug: UCCP_DEBUG_IF\n");
	if (uccp_debug & UCCP_DEBUG_80211IF)
		seq_puts(m, "***uccp_debug: UCCP_DEBUG_80211IF\n");
	if (uccp_debug & UCCP_DEBUG_RX)
		seq_puts(m, "***uccp_debug:UCCP_DEBUG_RX\n");
	if (uccp_debug & UCCP_DEBUG_HAL)
		seq_puts(m, "***uccp_debug:UCCP_DEBUG_HAL\n");
	if (uccp_debug & UCCP_DEBUG_CRYPTO)
		seq_puts(m, "***uccp_debug:UCCP_DEBUG_CRYPTO\n");
	if (uccp_debug & UCCP_DEBUG_DUMP_RX)
		seq_puts(m, "***uccp_debug: DUMP_RX Enabled\n");
	if (uccp_debug & UCCP_DEBUG_DUMP_HAL)
		seq_puts(m, "***uccp_debug: DUMP_HAL Enabled\n");
	if (uccp_debug & UCCP_DEBUG_TSMC)
		seq_puts(m, "***uccp_debug: UCCP_DEBUG_TSMC Enabled\n");

	seq_puts(m, "HELP: Add the values beside Module and\n");
	seq_puts(m, " echo uccp_debug=<SUM> to enable logging\n");
	seq_puts(m, " for those modules.\n");
	seq_puts(m, "MODULE (Value): TSMC (4096), DUMP_HAL (1024), DUMP_RX (512),\n");
	seq_puts(m, " CRYPTO(256), HAL(128), RX(64),\n");
	seq_puts(m, " 80211IF(32), UMAC_IF(16), CORE(8),\n");
	seq_puts(m, " TX(4), ROC(2), SCAN(1),\n");

	seq_puts(m, "To see the updated stats\n");
	seq_puts(m, "please run: echo get_stats=1 > /proc/uccp420/params\n");
	seq_puts(m, "To see the cleared phy stats\n");
	seq_puts(m, "please run: echo clear_stats=1 > /proc/uccp420/params\n");
	seq_puts(m, "************* VERSION ***********\n");
	seq_printf(m, "UCCP_DRIVER_VERSION = %s\n", UCCP_DRIVER_VERSION);

	if (wifi->hw &&
	    (((struct mac80211_dev *)(wifi->hw->priv))->state != STARTED)) {
		seq_printf(m, "LMAC_VERSION = %s\n", "UNKNOWN");
		seq_printf(m, "Firmware version = %s\n", "UNKNOWN");
	} else {
		seq_printf(m, "LMAC_VERSION = %s\n",
			   wifi->stats.uccp420_lmac_version);
		seq_printf(m, "Firmware version= %d.%d\n",
			   (wifi->stats.uccp420_lmac_version[0] - '0'),
			   (wifi->stats.uccp420_lmac_version[2] - '0'));
	}

	return 0;
}


static int proc_read_phy_stats(struct seq_file *m, void *v)
{

	int i = 0;

	seq_puts(m, "************* BB Stats ***********\n");
	seq_printf(m, "ed_cnt=%d\n",
		   wifi->stats.ed_cnt);
	seq_printf(m, "mpdu_cnt=%d\n",
		   wifi->stats.mpdu_cnt);
	seq_printf(m, "ofdm_crc32_pass_cnt=%d\n",
		   wifi->stats.ofdm_crc32_pass_cnt);
	seq_printf(m, "ofdm_crc32_fail_cnt=%d\n",
		   wifi->stats.ofdm_crc32_fail_cnt);
	seq_printf(m, "dsss_crc32_pass_cnt=%d\n",
		   wifi->stats.dsss_crc32_pass_cnt);
	seq_printf(m, "dsss_crc32_fail_cnt=%d\n",
		   wifi->stats.dsss_crc32_fail_cnt);
	seq_printf(m, "mac_id_pass_cnt=%d\n",
		   wifi->stats.mac_id_pass_cnt);
	seq_printf(m, "mac_id_fail_cnt=%d\n",
		   wifi->stats.mac_id_fail_cnt);
	seq_printf(m, "ofdm_corr_pass_cnt=%d\n",
		   wifi->stats.ofdm_corr_pass_cnt);
	seq_printf(m, "ofdm_corr_fail_cnt=%d\n",
		   wifi->stats.ofdm_corr_fail_cnt);
	seq_printf(m, "dsss_corr_pass_cnt=%d\n",
		   wifi->stats.dsss_corr_pass_cnt);
	seq_printf(m, "dsss_corr_fail_cnt=%d\n",
		   wifi->stats.dsss_corr_fail_cnt);
	seq_printf(m, "ofdm_s2l_fail_cnt=%d\n",
		   wifi->stats.ofdm_s2l_fail_cnt);
	seq_printf(m, "lsig_fail_cnt=%d\n",
		   wifi->stats.lsig_fail_cnt);
	seq_printf(m, "htsig_fail_cnt=%d\n",
		   wifi->stats.htsig_fail_cnt);
	seq_printf(m, "vhtsiga_fail_cnt=%d\n",
		   wifi->stats.vhtsiga_fail_cnt);
	seq_printf(m, "vhtsigb_fail_cnt=%d\n",
		   wifi->stats.vhtsigb_fail_cnt);
	seq_printf(m, "nonht_ofdm_cnt=%d\n",
		   wifi->stats.nonht_ofdm_cnt);
	seq_printf(m, "nonht_dsss_cnt=%d\n",
		   wifi->stats.nonht_dsss_cnt);
	seq_printf(m, "mm_cnt=%d\n",
		   wifi->stats.mm_cnt);
	seq_printf(m, "gf_cnt=%d\n",
		   wifi->stats.gf_cnt);
	seq_printf(m, "vht_cnt=%d\n",
		   wifi->stats.vht_cnt);
	seq_printf(m, "aggregation_cnt=%d\n",
		   wifi->stats.aggregation_cnt);
	seq_printf(m, "non_aggregation_cnt=%d\n",
		   wifi->stats.non_aggregation_cnt);
	seq_printf(m, "ndp_cnt=%d\n",
		   wifi->stats.ndp_cnt);
	seq_printf(m, "ofdm_ldpc_cnt=%d\n",
		   wifi->stats.ofdm_ldpc_cnt);
	seq_printf(m, "ofdm_bcc_cnt=%d\n",
		   wifi->stats.ofdm_bcc_cnt);
	seq_printf(m, "midpacket_cnt=%d\n",
		   wifi->stats.midpacket_cnt);
	seq_printf(m, "dsss_sfd_fail_cnt=%d\n",
		   wifi->stats.dsss_sfd_fail_cnt);
	seq_printf(m, "dsss_hdr_fail_cnt=%d\n",
		   wifi->stats.dsss_hdr_fail_cnt);
	seq_printf(m, "dsss_short_preamble_cnt=%d\n",
		   wifi->stats.dsss_short_preamble_cnt);
	seq_printf(m, "dsss_long_preamble_cnt=%d\n",
		   wifi->stats.dsss_long_preamble_cnt);
	seq_printf(m, "sifs_event_cnt=%d\n",
		   wifi->stats.sifs_event_cnt);
	seq_printf(m, "cts_cnt=%d\n",
		   wifi->stats.cts_cnt);
	seq_printf(m, "ack_cnt=%d\n",
		   wifi->stats.ack_cnt);
	seq_printf(m, "sifs_no_resp_cnt=%d\n",
		   wifi->stats.sifs_no_resp_cnt);
	seq_printf(m, "unsupported_cnt=%d\n",
		   wifi->stats.unsupported_cnt);
	seq_printf(m, "l1_corr_fail_cnt=%d\n",
		   wifi->stats.l1_corr_fail_cnt);
	seq_printf(m, "sifs_crc_exit_cnt=%d\n",
		wifi->stats.sifs_crc_exit_cnt);
	seq_printf(m, "low_energy_event_cnt=%d\n",
		wifi->stats.low_energy_event_cnt);
	seq_printf(m, "deagg_error_cnt=%d\n",
		wifi->stats.deagg_error_cnt);
	seq_printf(m, "nsymbols_error_cnt=%d\n",
		wifi->stats.nsymbols_error_cnt);
	seq_printf(m, "mcs32_cnt=%d\n",
		wifi->stats.mcs32_cnt);
	seq_printf(m, "ndpa_cnt=%d\n",
		wifi->stats.ndpa_cnt);
	seq_printf(m, "lsig_duration_error_cnt=%d\n",
		wifi->stats.lsig_duration_error_cnt);
	seq_printf(m, "rts_cnt=%d\n",
		wifi->stats.rts_cnt);
	seq_printf(m, "non_ht_cts_cnt=%d\n",
		wifi->stats.non_ht_cts_cnt);
	seq_printf(m, "rxp_active_exit_cnt=%d\n",
		wifi->stats.rxp_active_exit_cnt);
	seq_printf(m, "beamform_feedback_cnt=%d\n",
		wifi->stats.beamform_feedback_cnt);
	seq_printf(m, "self_cts_cnt=%d\n",
		wifi->stats.self_cts_cnt);
	seq_printf(m, "pop_master_cnt=%d\n",
		wifi->stats.pop_master_cnt);
	seq_printf(m, "pop_error_cnt=%d\n",
		wifi->stats.pop_error_cnt);
	seq_printf(m, "multicast_cnt=%d\n",
		wifi->stats.multicast_cnt);
	seq_printf(m, "tx_ed_abort_cnt=%d\n",
		wifi->stats.tx_ed_abort_cnt);
	seq_printf(m, "mcp_cts_cnt=%d\n",
		wifi->stats.mcp_cts_cnt);
	seq_printf(m, "deagg_q_post_cnt=%d\n",
		wifi->stats.deagg_q_post_cnt);
	seq_printf(m, "rxp_active_exit_dsss_cnt=%d\n",
		wifi->stats.rxp_active_exit_dsss_cnt);
	seq_printf(m, "rxp_extreme_error_cnt=%d\n",
		wifi->stats.rxp_extreme_error_cnt);
	seq_printf(m, "aci_fail_cnt=%d\n",
		wifi->stats.aci_fail_cnt);
	/* TX stats*/
	seq_printf(m, "tx_pkts_from_lmac = %d\n",
		   wifi->stats.tx_pkts_from_lmac);
	seq_printf(m, "tx_pkts_tx2tx = %d\n",
		   wifi->stats.tx_pkts_tx2tx);
	seq_printf(m, "tx_pkts_from_rx = %d\n",
		   wifi->stats.tx_pkts_from_rx);
	seq_printf(m, "tx_pkts_ofdm = %d\n",
		   wifi->stats.tx_pkts_ofdm);
	seq_printf(m, "tx_pkts_dsss = %d\n",
		   wifi->stats.tx_pkts_dsss);
	seq_printf(m, "tx_pkts_reached_end_of_fsm = %d\n",
		   wifi->stats.tx_pkts_reached_end_of_fsm);
	seq_printf(m, "tx_unsupported_modulation = %d\n",
		   wifi->stats.tx_unsupported_modulation);
	seq_printf(m, "tx_latest_pkt_from_lmac_or_sifs = %d\n",
		   wifi->stats.tx_latest_pkt_from_lmac_or_sifs);
	seq_printf(m, "tx_abort_bt_confirm_cnt = %d\n",
		   wifi->stats.tx_abort_bt_confirm_cnt);
	seq_printf(m, "tx_abort_txstart_timeout_cnt = %d\n",
		   wifi->stats.tx_abort_txstart_timeout_cnt);
	seq_printf(m, "tx_abort_midBT_cnt = %d\n",
		   wifi->stats.tx_abort_mid_bt_cnt);
	seq_printf(m, "tx_abort_dac_underrun_cnt = %d\n",
		   wifi->stats.tx_abort_dac_underrun_cnt);
	seq_printf(m, "tx_ofdm_symbols_master = %d\n",
		   wifi->stats.tx_ofdm_symbols_master);
	seq_printf(m, "tx_ofdm_symbols_slave1 = %d\n",
		   wifi->stats.tx_ofdm_symbols_slave1);
	seq_printf(m, "tx_ofdm_symbols_slave2 = %d\n",
		   wifi->stats.tx_ofdm_symbols_slave2);
	seq_printf(m, "tx_dsss_symbols = %d\n",
		   wifi->stats.tx_dsss_symbols);

	seq_puts(m, "************* RF Stats ***********\n");
	/*RF output data*/
	seq_puts(m, "rf_calib_data =");
	for (i = 0; i < wifi->stats.rf_calib_data_length; i++)
		seq_printf(m, "%02X", wifi->stats.rf_calib_data[i]);

	seq_puts(m, "\n");
	return 0;
}

static int proc_read_mac_stats(struct seq_file *m, void *v)
{
	unsigned int index;
	unsigned int total_samples = 0;
	unsigned int total_value = 0;
	int total_rssi_samples = 0;
	int total_rssi_value = 0;
	struct mac80211_dev *dev = NULL;

	if (ftm) {
		for (index = 0; index < MAX_AUX_ADC_SAMPLES; index++) {
			if (!wifi->params.pdout_voltage[index])
				continue;

			total_value += wifi->params.pdout_voltage[index];
			total_samples++;
		}
	}

	for (index = 0; index < MAX_RSSI_SAMPLES; index++) {

		if (!wifi->params.production_test)
			break;

		if (!wifi->params.rssi_average[index])
			continue;

		total_rssi_value += wifi->params.rssi_average[index];
		total_rssi_samples++;
	}

	seq_puts(m, "************* UMAC STATS ***********\n");
	seq_printf(m, "rx_packet_mgmt_count = %d\n",
		   wifi->stats.rx_packet_mgmt_count);
	seq_printf(m, "rx_packet_data_count = %d\n",
		   wifi->stats.rx_packet_data_count);
	seq_printf(m, "tx_packet_count(HT MCS0) = %d\n",
		   wifi->stats.ht_tx_mcs0_packet_count);
	seq_printf(m, "tx_packet_count(HT MCS1) = %d\n",
		   wifi->stats.ht_tx_mcs1_packet_count);
	seq_printf(m, "tx_packet_count(HT MCS2) = %d\n",
		   wifi->stats.ht_tx_mcs2_packet_count);
	seq_printf(m, "tx_packet_count(HT MCS3) = %d\n",
		   wifi->stats.ht_tx_mcs3_packet_count);
	seq_printf(m, "tx_packet_count(HT MCS4) = %d\n",
		   wifi->stats.ht_tx_mcs4_packet_count);
	seq_printf(m, "tx_packet_count(HT MCS5) = %d\n",
		   wifi->stats.ht_tx_mcs5_packet_count);
	seq_printf(m, "tx_packet_count(HT MCS6) = %d\n",
		   wifi->stats.ht_tx_mcs6_packet_count);
	seq_printf(m, "tx_packet_count(HT MCS7) = %d\n",
		   wifi->stats.ht_tx_mcs7_packet_count);

	if (wifi->params.uccp_num_spatial_streams == 2) {
		seq_printf(m, "tx_packet_count(HT MCS8) = %d\n",
			   wifi->stats.ht_tx_mcs8_packet_count);
		seq_printf(m, "tx_packet_count(HT MCS9) = %d\n",
			   wifi->stats.ht_tx_mcs9_packet_count);
		seq_printf(m, "tx_packet_count(HT MCS10) = %d\n",
			   wifi->stats.ht_tx_mcs10_packet_count);
		seq_printf(m, "tx_packet_count(HT MCS11) = %d\n",
			   wifi->stats.ht_tx_mcs11_packet_count);
		seq_printf(m, "tx_packet_count(HT MCS12) = %d\n",
			   wifi->stats.ht_tx_mcs12_packet_count);
		seq_printf(m, "tx_packet_count(HT MCS13) = %d\n",
			   wifi->stats.ht_tx_mcs13_packet_count);
		seq_printf(m, "tx_packet_count(HT MCS14) = %d\n",
			   wifi->stats.ht_tx_mcs14_packet_count);
		seq_printf(m, "tx_packet_count(HT MCS15) = %d\n",
			   wifi->stats.ht_tx_mcs15_packet_count);
	}
	if (vht_support) {
		seq_printf(m, "tx_packet_count(VHT MCS0) = %d\n",
			   wifi->stats.vht_tx_mcs0_packet_count);
		seq_printf(m, "tx_packet_count(VHT MCS1) = %d\n",
			   wifi->stats.vht_tx_mcs1_packet_count);
		seq_printf(m, "tx_packet_count(VHT MCS2) = %d\n",
			   wifi->stats.vht_tx_mcs2_packet_count);
		seq_printf(m, "tx_packet_count(VHT MCS3) = %d\n",
			   wifi->stats.vht_tx_mcs3_packet_count);
		seq_printf(m, "tx_packet_count(VHT MCS4) = %d\n",
			   wifi->stats.vht_tx_mcs4_packet_count);
		seq_printf(m, "tx_packet_count(VHT MCS5) = %d\n",
			   wifi->stats.vht_tx_mcs5_packet_count);
		seq_printf(m, "tx_packet_count(VHT MCS6) = %d\n",
			   wifi->stats.vht_tx_mcs6_packet_count);
		seq_printf(m, "tx_packet_count(VHT MCS7) = %d\n",
			   wifi->stats.vht_tx_mcs7_packet_count);
		seq_printf(m, "tx_packet_count(VHT MCS8) = %d\n",
			   wifi->stats.vht_tx_mcs8_packet_count);
		seq_printf(m, "tx_packet_count(VHT MCS9) = %d\n",
			   wifi->stats.vht_tx_mcs9_packet_count);
	}
	seq_printf(m, "tx_cmds_from_stack= %d\n",
		   wifi->stats.tx_cmds_from_stack);
	seq_printf(m, "tx_dones_to_stack= %d\n",
		   wifi->stats.tx_dones_to_stack);
	seq_printf(m, "oustanding_cmd_cnt = %d\n",
		   wifi->stats.outstanding_cmd_cnt);
	seq_printf(m, "gen_cmd_send_count = %d\n",
		   wifi->stats.gen_cmd_send_count);
	seq_printf(m, "umac_scan_req = %d\n",
		   wifi->stats.umac_scan_req);
	seq_printf(m, "umac_scan_complete = %d\n",
		   wifi->stats.umac_scan_complete);
	seq_printf(m, "tx_cmd_send_count_single = %d\n",
		   wifi->stats.tx_cmd_send_count_single);
	seq_printf(m, "tx_cmd_send_count_multi = %d\n",
		   wifi->stats.tx_cmd_send_count_multi);
	seq_printf(m, "tx_done_recv_count = %d\n",
		   wifi->stats.tx_done_recv_count);

	dev = (struct mac80211_dev *)(wifi->hw->priv);
	seq_printf(m, "tx_buff_pool_map = %ld\n",
		   dev->tx.buf_pool_bmp[0]);
	if (ftm)
		seq_printf(m, "pdout_val = %d (total samples: %d)\n",
			   total_samples ? (total_value/total_samples) : 0,
			   total_samples);
	if (wifi->params.production_test)
		seq_printf(m,
			   "rssi_average = %d dBm (total rssi samples: %d)\n",
			   total_rssi_samples ?
			   (total_rssi_value/total_rssi_samples) : 0,
			   total_rssi_samples);

	seq_puts(m, "************* LMAC STATS ***********\n");
	seq_printf(m, "roc_start =%d\n",
		   wifi->stats.roc_start);
	seq_printf(m, "roc_stop =%d\n",
		   wifi->stats.roc_stop);
	seq_printf(m, "roc_complete =%d\n",
		   wifi->stats.roc_complete);
	seq_printf(m, "roc_stop_complete =%d\n",
		   wifi->stats.roc_stop_complete);
	/* TX related */
	seq_printf(m, "tx_cmd_cnt =%d\n",
		   wifi->stats.tx_cmd_cnt);
	seq_printf(m, "tx_done_cnt =%d\n",
		   wifi->stats.tx_done_cnt);
	seq_printf(m, "tx_edca_trigger_cnt =%d\n",
		   wifi->stats.tx_edca_trigger_cnt);
	seq_printf(m, "tx_edca_isr_cnt =%d\n",
		   wifi->stats.tx_edca_isr_cnt);
	seq_printf(m, "tx_start_cnt =%d\n",
		   wifi->stats.tx_start_cnt);
	seq_printf(m, "tx_abort_cnt =%d\n",
		   wifi->stats.tx_abort_cnt);
	seq_printf(m, "tx_abort_isr_cnt =%d\n",
		   wifi->stats.tx_abort_isr_cnt);
	seq_printf(m, "tx_underrun_cnt =%d\n",
		   wifi->stats.tx_underrun_cnt);
	seq_printf(m, "tx_rts_cnt =%d\n",
		   wifi->stats.tx_rts_cnt);
	seq_printf(m, "tx_ampdu_cnt =%d\n",
		   wifi->stats.tx_ampdu_cnt);
	seq_printf(m, "tx_mpdu_cnt =%d\n",
		   wifi->stats.tx_mpdu_cnt);

	/* RX related */
	seq_printf(m, "rx_isr_cnt  =%d\n",
		   wifi->stats.rx_isr_cnt);
	seq_printf(m, "rx_ack_cts_to_cnt =%d\n",
		   wifi->stats.rx_ack_cts_to_cnt);
	seq_printf(m, "rx_cts_cnt =%d\n",
		   wifi->stats.rx_cts_cnt);
	seq_printf(m, "rx_ack_resp_cnt =%d\n",
		   wifi->stats.rx_ack_resp_cnt);
	seq_printf(m, "rx_ba_resp_cnt =%d\n",
		   wifi->stats.rx_ba_resp_cnt);
	seq_printf(m, "rx_fail_in_ba_bitmap_cnt =%d\n",
		   wifi->stats.rx_fail_in_ba_bitmap_cnt);
	seq_printf(m, "rx_circular_buffer_free_cnt =%d\n",
		   wifi->stats.rx_circular_buffer_free_cnt);
	seq_printf(m, "rx_mic_fail_cnt =%d\n",
		   wifi->stats.rx_mic_fail_cnt);

	/* HAL related */
	seq_printf(m, "hal_cmd_cnt  =%d\n",
		   wifi->stats.hal_cmd_cnt);
	seq_printf(m, "hal_event_cnt =%d\n",
		   wifi->stats.hal_event_cnt);
	seq_printf(m, "hal_ext_ptr_null_cnt =%d\n",
		   wifi->stats.hal_ext_ptr_null_cnt);

	return 0;

}


void uccp420wlan_reinit(void)
{

	if (wifi->hw)
		uccp420wlan_exit();
	uccp420wlan_init();
	uccp_reinit = 1;
}
static ssize_t proc_write_config(struct file *file,
				 const char __user *buffer,
				 size_t count,
				 loff_t *ppos)
{
	char buf[(RF_PARAMS_SIZE * 2) + 50];
	unsigned long val;
	long sval;
	unsigned int rate = wifi->params.prod_mode_rate_flag;
	unsigned int b40 = wifi->params.prod_mode_chnl_bw_40_mhz;
	unsigned int b80 = wifi->params.prod_mode_chnl_bw_80_mhz;
	struct mac80211_dev *dev = wifi->hw->priv;

	if (count >= sizeof(buf))
		count = sizeof(buf) - 1;

	if (copy_from_user(buf, buffer, count))
		return -EFAULT;

	buf[count] = '\0';

	if (param_get_val(buf, "dot11a_support=", &val)) {
		if (((val == 0) || (val == 1)) &&
		    (wifi->params.dot11a_support != val)) {
			wifi->params.dot11a_support = val;

			if ((wifi->params.dot11g_support == 0) &&
			    (wifi->params.dot11a_support == 0)) {
				pr_err("Invalid parameter value. Both bands can't be disabled, at least 1 is needed\n");
			} else {
					uccp420wlan_reinit();
					pr_info("Re-initializing UMAC ..with 2.4GHz support %s and 5GHz support %s\n",
					wifi->params.dot11g_support == 0 ?
					"disabled" : "enabled",
					wifi->params.dot11a_support == 0 ?
					"disabled" : "enabled");

			}
		} else
			pr_err("Invalid parameter value\n");
	} else if (param_get_val(buf, "dot11g_support=", &val)) {
		if (((val == 0) || (val == 1)) &&
		    (wifi->params.dot11g_support != val)) {
			wifi->params.dot11g_support = val;

			if ((wifi->params.dot11g_support == 0) &&
			    (wifi->params.dot11a_support == 0)) {
				pr_err("Invalid parameter value. Both bands can't be disabled, at least 1 is needed\n");
			} else {
					uccp420wlan_reinit();
					pr_info("Re-initializing UMAC ..with 2.4GHz support %s and 5GHz support %s\n",
					wifi->params.dot11g_support == 0 ?
					"disabled" : "enabled",
					wifi->params.dot11a_support == 0 ?
					"disabled" : "enabled");

			}
		} else
			pr_err("Invalid parameter value\n");
	} else if (param_get_sval(buf, "sensitivity=", &sval)) {
		/*if (sval > -51 || sval < -96 || (sval % 3 != 0))*/
		/*	pr_err("Invalid parameter value\n");*/
		/*else*/
		wifi->params.ed_sensitivity = sval;
	} else if (param_get_val(buf, "auto_sensitivity=", &val)) {
		if ((val == 0) || (val == 1))
			wifi->params.auto_sensitivity = val;
		else
			pr_err("Invalid parameter value.\n");
	} else if (param_get_val(buf, "production_test=", &val)) {
		if ((val == 0) || (val == 1)) {
			if (wifi->params.production_test != val) {
				if (wifi->params.production_test)
					wifi->params.num_vifs = 1;

				wifi->params.production_test = val;

				uccp420wlan_reinit();
				pr_err("Re-initializing UMAC ..\n");
			}
		} else
			pr_err("Invalid parameter value\n");
	} else if (param_get_val(buf, "bypass_vpd=", &val)) {
		if ((val == 0) || (val == 1)) {
			if (wifi->params.bypass_vpd != val)
				wifi->params.bypass_vpd = val;
		} else
			pr_err("Invalid parameter value\n");
	} else if (param_get_val(buf, "num_vifs=", &val)) {
		if (val > 0 && val <= MAX_VIFS) {
			if (wifi->params.num_vifs != val) {
				uccp420wlan_reinit();
				pr_err("Re-initializing UMAC ..\n");
				wifi->params.num_vifs = val;
			}
		}
	} else if (param_get_match(buf, "rf_params=")) {
		conv_str_to_byte(wifi->params.rf_params,
				strstr(buf, "=") + 1,
				RF_PARAMS_SIZE);
	} else if (param_get_val(buf, "rx_packet_mgmt_count=", &val)) {
		wifi->stats.rx_packet_mgmt_count = val;
	} else if (param_get_val(buf, "rx_packet_data_count=", &val)) {
		wifi->stats.rx_packet_data_count = val;
	} else if (param_get_val(buf, "pdout_val=", &val)) {
		wifi->stats.pdout_val = val;
	} else if (param_get_val(buf, "get_stats=", &val)) {
		if (dev->state != STARTED) {
			pr_err("Interface is not initialized\n");
			goto error;
		}
		uccp420wlan_prog_mib_stats();
	} else if (param_get_val(buf, "max_data_size=", &val)) {
		if (wifi->params.max_data_size != val) {
			if ((wifi->params.max_data_size >= 2 * 1024) &&
			    (wifi->params.max_data_size <= (12 * 1024))) {
				wifi->params.max_data_size = val;

				uccp420wlan_reinit();
				pr_err("Re-initalizing UCCP420 with %ld as max data size\n",
				       val);

			} else
				pr_err("Invalid Value for max data size: should be (2K-12K)\n");
		}
	} else if (param_get_val(buf, "max_tx_cmds=", &val)) {
		int max_tx_cmd_limit = 0;

		if (vht_support)
			max_tx_cmd_limit = MAX_SUBFRAMES_IN_AMPDU_VHT;
		else
			max_tx_cmd_limit = MAX_SUBFRAMES_IN_AMPDU_HT;

		if (val >= 1 && val <= max_tx_cmd_limit)
			wifi->params.max_tx_cmds = val;
		else
			pr_err("Please enter value between 1 and %d\n",
			       max_tx_cmd_limit);
	} else if (param_get_val(buf, "disable_power_save=", &val)) {
		if ((val == 0) || (val == 1)) {
			if (val != wifi->params.disable_power_save) {
				wifi->params.disable_power_save = val;

				uccp420wlan_reinit();
				pr_err("Re-initalizing UCCP420 with global powerave %s\n",
				       val ? "DISABLED" : "ENABLED");
			}
		}
	} else if (param_get_val(buf, "disable_sm_power_save=", &val)) {
		if ((val == 0) || (val == 1)) {
			if (val != wifi->params.disable_sm_power_save) {
				wifi->params.disable_sm_power_save = val;

				uccp420wlan_reinit();
				pr_err("Re-initalizing UCCP420 with smps %s\n",
				       val ? "DISABLED" : "ENABLED");

			}
		}
	} else if (param_get_val(buf, "uccp_num_spatial_streams=", &val)) {
		if (val > 0 && val <= min(MAX_TX_STREAMS, MAX_RX_STREAMS)) {
			if (val != wifi->params.uccp_num_spatial_streams) {
				wifi->params.uccp_num_spatial_streams = val;
				wifi->params.num_spatial_streams = val;
				wifi->params.max_tx_streams = val;
				wifi->params.max_rx_streams = val;
				uccp420wlan_reinit();
				pr_err("Re-initalizing UCCP420 with %ld spatial streams\n",
				       val);
			}
		} else
			pr_err("Invalid parameter value: Allowed Range: 1 to %d\n",
			       min(MAX_TX_STREAMS, MAX_RX_STREAMS));
	} else if (param_get_val(buf, "antenna_sel=", &val)) {
		if (val == 1 || val == 2) {
			if (val != wifi->params.antenna_sel) {
				wifi->params.antenna_sel = val;
				uccp420wlan_reinit();
				pr_err("Re-initalizing UCCP420 with %ld antenna selection\n",
				       val);
			}
		} else
			pr_err("Invalid parameter value: Allowed Values: 1 or 2\n");
	} else if (param_get_val(buf, "num_spatial_streams=", &val)) {
		if (val > 0 && val <= wifi->params.uccp_num_spatial_streams)
			wifi->params.num_spatial_streams = val;
		else
			pr_err("Invalid parameter value, should be less than or equal to uccp_num_spatial_streams\n");
	} else if (param_get_sval(buf, "mgd_mode_tx_fixed_mcs_indx=", &sval)) {
		if (wifi->params.mgd_mode_tx_fixed_rate == -1) {

			int mcs_indx = wifi->params.mgd_mode_tx_fixed_mcs_indx;

			if (vht_support && (wifi->params.prod_mode_rate_flag &
					    ENABLE_VHT_FORMAT)) {
				if ((sval >= -1) && (sval <= 9)) {
					/* Get_rate will do the MCS holes
					 * validation
					 */
					mcs_indx = sval;
				} else
					pr_err("Invalid parameter value.\n");
			} else {
				if (wifi->params.num_spatial_streams == 2) {
					if ((sval >= -1) && (sval <= 15))
						mcs_indx = sval;
					else
						pr_err("Invalid MIMO HT MCS: %ld\n",
						       sval);
				}
				if (wifi->params.num_spatial_streams == 1) {
					if ((sval >= -1) && (sval <= 7))
						mcs_indx = sval;
					else
						pr_err("Invalid SISO HT MCS: %ld\n",
						       sval);
				}
			}

			wifi->params.mgd_mode_tx_fixed_mcs_indx = mcs_indx;
		} else
			pr_err("Fixed rate other than MCS index is currently set\n");
	} else if (param_get_sval(buf, "mgd_mode_tx_fixed_rate=", &sval)) {
		if (wifi->params.mgd_mode_tx_fixed_mcs_indx == -1) {
			int tx_fixed_rate = wifi->params.mgd_mode_tx_fixed_rate;

			if (wifi->params.dot11g_support == 1 &&
			    ((sval == 1) ||
			     (sval == 2) ||
			     (sval == 55) ||
			     (sval == 11))) {
				tx_fixed_rate = sval;
			} else if ((sval == 6) ||
				   (sval == 9) ||
				   (sval == 12) ||
				   (sval == 18) ||
				   (sval == 24) ||
				   (sval == 36) ||
				   (sval == 48) ||
				   (sval == 54) ||
				   (sval == -1)) {
				tx_fixed_rate = sval;
			} else {
				pr_err("Invalid parameter value.\n");
				return count;
			}
			wifi->params.mgd_mode_tx_fixed_rate = tx_fixed_rate;
		} else
			pr_err("MCS data rate(index) is currently set\n");
	} else if (param_get_sval(buf, "tx_fixed_mcs_indx=", &sval)) {
		if (wifi->params.production_test != 1) {
			pr_err("Only can be set in production mode.\n");
			goto error;
		}

		if (sval == -1) {
			wifi->params.tx_fixed_mcs_indx = -1;
			goto error;
		}

		if (wifi->params.tx_fixed_rate != -1) {
			pr_err("Fixed rate other than MCS index is currently set\n");
			goto error;
		}
		if (vht_support && (rate & ENABLE_VHT_FORMAT)) {
			if ((sval >= -1) && (sval <= 9)) {
				if ((b40 == 0) && (b80 == 0) && (sval == 9)) {
					pr_err("Invalid VHT MCS: 20MHZ-MCS9.\n");
					/*Reset to Default*/
					wifi->params.tx_fixed_mcs_indx = 7;
				} else
					wifi->params.tx_fixed_mcs_indx = sval;
			} else
				pr_err("Invalid parameter value.\n");
		} else if (vht_support && (rate & ENABLE_11N_FORMAT)) {
			if (wifi->params.num_spatial_streams == 2) {
				if ((sval >= -1) && (sval <= 15))
					wifi->params.tx_fixed_mcs_indx = sval;
				else
					pr_err("Invalid MIMO HT MCS: %ld\n",
						sval);
			} else if (wifi->params.num_spatial_streams == 1) {
				if ((sval >= -1) && (sval <= 7))
					wifi->params.tx_fixed_mcs_indx = sval;
				else
					pr_err("Invalid SISO HT MCS: %ld\n",
						sval);
			}
		} else
			pr_err("MCS Setting is invalid for Legacy, please set prod_mode_rate_flag first.\n");

	} else if (param_get_sval(buf, "tx_fixed_rate=", &sval)) {
		if (wifi->params.production_test != 1) {
			pr_err("Only can be set in production mode.\n");
			goto error;
		}

		if (sval == -1) {
			wifi->params.tx_fixed_rate = -1;
			goto error;
		}
		if (wifi->params.tx_fixed_mcs_indx != -1) {
			pr_err("MCS Index is currently set.\n");
			goto error;
		}

		if ((wifi->params.dot11g_support == 1) &&
			    ((sval == 1) ||
			     (sval == 2) ||
			     (sval == 55) ||
			     (sval == 11))) {
				wifi->params.tx_fixed_rate = sval;
		} else if ((sval == 6) ||
			   (sval == 9) ||
			   (sval == 12) ||
			   (sval == 18) ||
			   (sval == 24) ||
			   (sval == 36) ||
			   (sval == 48) ||
			   (sval == 54) ||
			   (sval == -1)) {
				wifi->params.tx_fixed_rate = sval;
		} else {
			pr_err("Invalid parameter value: tx_fixed_rate=%ld\n",
				sval);
			goto error;
		}
	} else if (param_get_val(buf, "chnl_bw=", &val)) {
		if (((val == 0) ||
		    (vht_support && (val == 2)) ||
		     (val == 1))) {
			wifi->params.chnl_bw = val;

			uccp420wlan_reinit();
			pr_err("Re-initializing UMAC ..\n");
		} else
			pr_err("Invalid parameter value.\n");
	} else if (param_get_val(buf, "prod_mode_chnl_bw_40_mhz=", &val)) {

		do {
			if (wifi->params.production_test != 1) {
				pr_err("Can be set in only in production mode.\n");
				break;
			}

			if (!((val == 0) || (val == 1))) {
				pr_err("Invalid parameter value.\n");
				break;
			}

			wifi->params.prod_mode_chnl_bw_40_mhz = val;

			if (!vht_support)
				break;

			wifi->params.prod_mode_chnl_bw_80_mhz = 0;
		} while (0);

	} else if (vht_support &&
		   param_get_val(buf, "prod_mode_chnl_bw_80_mhz=", &val)) {
		if (wifi->params.production_test == 1) {
			if ((val == 0) || (val == 1)) {
				wifi->params.prod_mode_chnl_bw_40_mhz = 0;
				wifi->params.prod_mode_chnl_bw_80_mhz = val;
			} else
				pr_err("Invalid parameter value.\n");
		} else
			pr_err("Can be set in only in production mode.\n");
	} else if (param_get_val(buf, "sec_ch_offset_40_plus=", &val)) {
		do {
			if (wifi->params.production_test != 1) {
				pr_err("Can be set in only in production mode.\n");
				break;
			}

			if (val == 0) {
				wifi->params.sec_ch_offset_40_plus = 0;
				goto error;
			}

			if (!((wifi->params.prod_mode_chnl_bw_40_mhz == 1)
			    || (vht_support &&
				(wifi->params.prod_mode_chnl_bw_80_mhz == 1))
			    )) {
				pr_err("Can be set only when prod_mode_chnl_bw_40_mhz is set.\n");
				break;
			}


			if (wifi->params.sec_ch_offset_40_minus == 1) {
				pr_err("Can be set only when sec_ch_offset_40_minus is not set\n");
				break;
			}

			if (!((val == 0) || (val == 1))) {
				pr_err("Invalid parameter value.\n");
				break;
			}

			wifi->params.sec_ch_offset_40_plus = val;

		} while (0);

	} else if (param_get_val(buf, "sec_ch_offset_40_minus=", &val)) {
		do {
			if (wifi->params.production_test != 1) {
				pr_err("Can be set in only in production mode.\n");
				break;
			}

			if (val == 0) {
				wifi->params.sec_ch_offset_40_minus = 0;
				goto error;
			}

			if (!((wifi->params.prod_mode_chnl_bw_40_mhz == 1)
			    || (vht_support &&
				(wifi->params.prod_mode_chnl_bw_80_mhz == 1))
			    )) {
				pr_err("Can be set only when prod_mode_chnl_bw_40_mhz is set.\n");
				break;
			}


			if (wifi->params.sec_ch_offset_40_plus == 1) {
				pr_err("Can be set only when sec_ch_offset_40_plus is not set\n");
				break;
			}

			if (!((val == 0) || (val == 1))) {
				pr_err("Invalid parameter value.\n");
				break;
			}

			wifi->params.sec_ch_offset_40_minus = val;

		} while (0);

	} else if (vht_support &&
		   param_get_val(buf, "sec_40_ch_offset_80_plus=", &val)) {
		do {
			if (wifi->params.production_test != 1) {
				pr_err("Can be set in only in production mode.\n");
				break;
			}

			if (val == 0) {
				wifi->params.sec_40_ch_offset_80_plus = 0;
				goto error;
			}

			if (!(wifi->params.prod_mode_chnl_bw_80_mhz == 1)) {
				pr_err("Can be set only when prod_mode_chnl_bw_80_mhz is set\n");
				break;
			}


			if (wifi->params.sec_40_ch_offset_80_minus == 1) {
				pr_err("Can be set only when sec_40_ch_offset_80_minus is not set\n");
				break;
			}

			if (!((val == 0) || (val == 1))) {
				pr_err("Invalid parameter value.\n");
				break;
			}

			wifi->params.sec_40_ch_offset_80_plus = val;

		} while (0);

	} else if (vht_support &&
		   (param_get_val(buf, "sec_40_ch_offset_80_minus=", &val))) {
		do {
			if (wifi->params.production_test != 1) {
				pr_err("Can be set in only in production mode.\n");
				break;
			}

			if (val == 0) {
				wifi->params.sec_40_ch_offset_80_minus = 0;
				goto error;
			}
			if (!(wifi->params.prod_mode_chnl_bw_80_mhz == 1)) {
				pr_err("Can be set if prod_mode_chnl_bw_80_mhz is set\n");
				break;
			}


			if (wifi->params.sec_40_ch_offset_80_plus == 1) {
				pr_err("Can be set only when sec_40_ch_offset_80_plus is not set\n");
				break;
			}

			if (!((val == 0) || (val == 1))) {
				pr_err("Invalid parameter value.\n");
				break;
			}

			wifi->params.sec_40_ch_offset_80_minus = val;

		} while (0);
	} else if (param_get_val(buf, "prod_mode_rate_flag=", &val)) {
		do {
			/*Only first 6 flags are defined currently*/
			if (val > 63)
				pr_err("Invalid parameter value");

			if ((val & ENABLE_VHT_FORMAT) &&
			    (val & ENABLE_11N_FORMAT)) {
				pr_err("Cannot set HT and VHT both.");
				break;
			}

			if ((val & ENABLE_CHNL_WIDTH_40MHZ) &&
			    (val & ENABLE_CHNL_WIDTH_80MHZ)) {
				pr_err("Cannot set 40 and 80 both.");
				break;
			}

			if ((wifi->params.uccp_num_spatial_streams == 1)  &&
			    (val & ENABLE_SGI) &&
			    (val & ENABLE_GREEN_FIELD)) {
				pr_err("Cannot set GreenField when SGI is enabled for SISO");
				break;
			}

			wifi->params.prod_mode_rate_flag = val;
		} while (0);

	} else if (param_get_val(buf, "rate_protection_type=", &val)) {
		/* 0 is None, 1 is RTS/CTS, 2 is for CTS2SELF */
		if ((val == 0) || (val == 1) /*|| (val == 2)*/)
			wifi->params.rate_protection_type = val;
		else
			pr_err("Invalid parameter value");
	} else if (param_get_val(buf, "prod_mode_rate_preamble_type=", &val)) {
		/*0 is short, 1 is Long*/
		if ((val == 0) || (val == 1))
			wifi->params.prod_mode_rate_preamble_type = val;
		else
			pr_err("Invalid parameter value");
	} else if (param_get_val(buf, "prod_mode_stbc_enabled=", &val)) {
		if (val <= 1)
			wifi->params.prod_mode_stbc_enabled = val;
		else
			pr_err("Invalid parameter value\n");
	} else if (param_get_val(buf, "prod_mode_bcc_or_ldpc=", &val)) {
		if (val <= 1)
			wifi->params.prod_mode_bcc_or_ldpc = val;
		else
			pr_err("Invalid parameter value\n");
	} else if (param_get_val(buf, "reset_hal_params=", &val)) {
		if (dev->state != STARTED) {
			if (val != 1)
				pr_err("Invalid parameter value\n");
			else
				hal_ops.reset_hal_params();
		} else
			pr_err("HAL parameters reset can be done only when all interface are down\n");
	} else if (param_get_val(buf, "vht_beamformer_enable=", &val)) {
		do {
			int vht_beamform_period;

			if (wifi->params.vht_beamform_enable == val)
				break;

			if (!((val == VHT_BEAMFORM_ENABLE) ||
			      (val == VHT_BEAMFORM_DISABLE))) {
				pr_err("Invalid VHT Beamforming Enable value should be 1 or 0\n");
				break;
			}

			wifi->params.vht_beamform_enable = val;

			/* If not associated, it will be sent upon
			 * association
			 */
			if (!wifi->params.is_associated)
				break;

			if (!wifi->params.vht_beamform_support) {
				pr_err("Peer doesn't support VHT Beamformee.\n");
				break;
			}

			vht_beamform_period = wifi->params.vht_beamform_period;

			if (dev->state != STARTED) {
				pr_err("Interface is not initialized\n");
				goto error;
			}

			uccp420wlan_prog_vht_bform(val, vht_beamform_period);
		} while (0);

	} else if (param_get_val(buf, "vht_beamformer_period=", &val)) {

		do {
			int vht_beamform_enable;

			if (wifi->params.vht_beamform_enable !=
			    VHT_BEAMFORM_ENABLE) {
				pr_err("VHT Beamforming is disabled, please enable it first\n");
				break;
			}

			if (wifi->params.vht_beamform_period == val)
				break;

			if (!((val > 100) || (val < 10000))) {
				pr_err("Invalid VHT Beamforming Period must be between 100-10000ms\n");
				break;
			}

			wifi->params.vht_beamform_period = val;

			/* If not associated, it will be sent upon
			 * association
			 */
			if (!wifi->params.is_associated)
				break;

			if (!wifi->params.vht_beamform_support) {
				pr_err("Peer doesn't support VHT Beamformee.\n");
				break;
			}

			vht_beamform_enable = wifi->params.vht_beamform_enable;

			if (dev->state != STARTED) {
				pr_err("Interface is not initialized\n");
				goto error;
			}

			uccp420wlan_prog_vht_bform(vht_beamform_enable, val);
		} while (0);

	} else if (param_get_val(buf, "bg_scan_enable=", &val)) {
		if (wifi->params.bg_scan_enable != val) {
			if ((val == 1) || (val == 0)) {
				wifi->params.bg_scan_enable = val;

				uccp420wlan_reinit();
				pr_err("Re-initializing UMAC ..\n");
			} else
				pr_err("Invalid bg_scan_enable value should be 1 or 0\n");
		}
	} else if (param_get_match(buf, "bg_scan_channel_list=")) {
		conv_str_to_byte(wifi->params.bg_scan_channel_list,
				 strstr(buf, "=") + 1,
				 50);
	} else if (param_get_match(buf, "bg_scan_channel_flags=")) {
		conv_str_to_byte(wifi->params.bg_scan_channel_flags,
				 strstr(buf, "=") + 1,
				 50);
	} else if (param_get_val(buf, "bg_scan_intval=", &val)) {
		if ((val >= 1000) && (val <= 60000))
			wifi->params.bg_scan_intval = val * 1000;/* us */
		else
			pr_err("Invalid bgscan duration/interval value should be between 1000 to 60000 ms.\n");
#if 0
		/*currently not used in LMAC, so don't export to user*/
	} else if (param_get_val(buf, "bg_scan_chan_dur =", &val)) {
		if ((val >= 100) && (val <= 1000))
			wifi->params.bg_scan_chan_dur = val;
		else
			pr_err("Invalid chan duration value should be between 100 to 1000.\n");
	} else if (param_get_val(buf, "bg_scan_serv_chan_dur =", &val)) {
		if ((val >= 100) && (val <= 1000))
			wifi->params.bg_scan_serv_chan_dur = val;
		else
			pr_err("Invalid serv chan duration value should be between 100 to 1000.\n");
#endif
	} else if (param_get_val(buf, "bg_scan_num_channels=", &val)) {
		wifi->params.bg_scan_num_channels = val;
	} else if (param_get_val(buf, "nw_selection=", &val)) {

		if (dev->state != STARTED) {
			pr_err("Interface is not initialized\n");
			goto error;
		}

		if ((val == 1) || (val == 0)) {
			wifi->params.nw_selection = val;
			pr_err("in nw_selection\n");
			uccp420wlan_prog_nw_selection(1, vif_macs[0]);
		} else
			pr_err("Invalid nw selection value should be 1 or 0\n");
	} else if (param_get_val(buf, "scan_type=", &val)) {
		if ((val == 0) || (val == 1))
			wifi->params.scan_type = val;
		else
			pr_err("Invalid scan type value %d, should be 0 or 1\n",
			       (unsigned int)val);
	} else if (ftm && param_get_val(buf, "aux_adc_chain_id=", &val)) {

		if (dev->state != STARTED) {
			pr_err("Interface is not initialized\n");
			goto error;
		}

		memset(wifi->params.pdout_voltage, 0,
		       sizeof(char) * MAX_AUX_ADC_SAMPLES);
		if ((val == AUX_ADC_CHAIN1) || (val == AUX_ADC_CHAIN2)) {
			wifi->params.aux_adc_chain_id = val;
			uccp420wlan_prog_aux_adc_chain(val);
		} else
			pr_err("Invalid chain id %d, should be %d or %d\n",
			       (unsigned int) val,
			       AUX_ADC_CHAIN1,
			       AUX_ADC_CHAIN2);
	} else if (param_get_val(buf, "continuous_tx=", &val)) {
		if (wifi->params.production_test != 1) {
			pr_err("continuous_tx: Can be set in only in production mode.\n");
			goto error;
		}

		if (dev->state != STARTED) {
			pr_err("Interface is not initialized\n");
			goto error;
		}

		if (val == 0 || val == 1) {
			wifi->params.cont_tx = val;
			uccp420wlan_prog_cont_tx(val);
		} else
			pr_err("Invalid tx_continuous parameter\n");
	} else if (param_get_val(buf, "start_prod_mode=", &val)) {
			unsigned int pri_chnl_num = 0;
			unsigned int freq_band = IEEE80211_BAND_5GHZ;
			int center_freq = 0;

			if (wifi->params.production_test != 1) {
				pr_err("start_prod_mode: Can be set in only in production mode.\n");
				goto error;
			}

			if (wifi->params.init_prod) {
				pr_err("Production Test is already initialized.\n");
				goto error;
			}

			pri_chnl_num = val;
			wifi->params.start_prod_mode = val;
			tasklet_init(&dev->proc_tx_tasklet, packet_generation,
				     (unsigned long)dev);
			if (pri_chnl_num < 15)
				freq_band = IEEE80211_BAND_2GHZ;
			else
				freq_band = IEEE80211_BAND_5GHZ;

			center_freq =
			ieee80211_channel_to_frequency(pri_chnl_num,
						       freq_band);

			if ((wifi->params.fw_loading == 1) &&
			     load_fw(dev->hw)) {
				pr_err("%s: Firmware loading failed\n",
				       dev->name);
				goto error;
			}

			if (!uccp420wlan_core_init(dev, ftm)) {
				uccp420wlan_prog_vif_ctrl(0,
						dev->if_mac_addresses[0].addr,
						IF_MODE_STA_IBSS,
						IF_ADD);

				proc_bss_info_changed(
						dev->if_mac_addresses[0].addr,
						val);

				uccp420wlan_prog_channel(pri_chnl_num,
							center_freq,
							 0,
							 0,
					/*It will be overwritten anyway*/
#ifdef MULTI_CHAN_SUPPORT
							 0,
#endif
							 freq_band);
				skb_queue_head_init(&dev->tx.proc_tx_list[0]);
				wifi->params.init_prod = 1;
				dev->state = STARTED;
				uccp_reinit = 0;
			 } else {
				pr_err("RPU Initialization Failed\n");
				wifi->params.init_prod = 0;
			}

	} else if (param_get_sval(buf, "stop_prod_mode=", &sval)) {

			if (!wifi->params.init_prod) {
				DEBUG_LOG("Prod mode is not initialized\n");
				goto error;
			}

			tasklet_kill(&dev->proc_tx_tasklet);
#if 0
			/* Todo: Enabling this causes RPU Lockup,
			 * need to debug
			 */
			uccp420wlan_prog_vif_ctrl(0,
						  dev->if_mac_addresses[0].addr,
						  IF_MODE_STA_IBSS,
						  IF_REM);
#endif
			if (!uccp_reinit)
				stop(wifi->hw);

			wifi->params.start_prod_mode = 0;
			wifi->params.pkt_gen_val = 1;
			wifi->params.init_prod = 0;
			wifi->params.init_pkt_gen = 0;
	} else if (param_get_sval(buf, "start_packet_gen=", &sval)) {


		if (!wifi->params.init_prod) {
			pr_err("NEW Production Mode is not Initialized\n");
			goto error;
		}

		if (wifi->params.init_pkt_gen) {
			pr_err("packet gen is already running\n");
			goto error;
		}

		if (wifi->params.tx_fixed_mcs_indx == -1 &&
			wifi->params.tx_fixed_rate == -1) {
			pr_err("Either tx_fixed_mcs_index Or tx_fixed_rate should be set, both can't be NULL.\n");
			goto error;
		}

		wifi->params.init_pkt_gen = 1;

		wifi->params.pkt_gen_val = sval;
		if (sval != 0)
			tasklet_schedule(&dev->proc_tx_tasklet);

	} else if (param_get_sval(buf, "stop_packet_gen=", &sval)) {

		if (!wifi->params.init_prod) {
			DEBUG_LOG("NEW Production Mode is not Initialized\n");
			goto error;
		}

		wifi->params.pkt_gen_val = 1;
		wifi->params.init_pkt_gen = 0;
		tasklet_kill(&dev->proc_tx_tasklet);
	} else if (param_get_val(buf, "payload_length=", &val)) {
		wifi->params.payload_length = val;
	} else if (param_get_sval(buf, "set_tx_power=", &sval)) {
		if (wifi->params.production_test != 1 && !ftm) {
			pr_err("set_tx_power: Can be set in only in FTM/production mode.\n");
			goto error;
		}

		if (!wifi->params.init_prod) {
			DEBUG_LOG("NEW Production Mode is not Initialized\n");
			goto error;
		}

		memset(wifi->params.pdout_voltage, 0,
		       sizeof(char) * MAX_AUX_ADC_SAMPLES);
		wifi->params.set_tx_power = sval;
		uccp420wlan_prog_txpower(sval);
#ifdef PERF_PROFILING
	} else if (param_get_val(buf, "driver_tput=", &val)) {
		if ((val == 1) || (val == 0))
			wifi->params.driver_tput = val;
		else
			pr_err("Invalid driver_tput value should be 1 or 0\n");
#endif
	} else if (param_get_val(buf, "fw_loading=", &val)) {
			wifi->params.fw_loading = val;
	} else if (param_get_val(buf, "bt_state=", &val)) {
		if (dev->state != STARTED) {
			pr_err("Interface is not initialized\n");
			goto error;
		}

		if (val == 0 || val == 1) {
			if (val != wifi->params.bt_state) {
				wifi->params.bt_state = val;
				uccp420wlan_prog_btinfo(val);
			}
		} else
			pr_err("Invalid parameter value: Allowed values: 0 or 1\n");
	} else if (param_get_val(buf, "clear_stats=", &val)) {
		if (dev->state != STARTED) {
			pr_err("Interface is not initialized\n");
			goto error;
		}
		uccp420wlan_prog_clear_stats();
	} else if (param_get_val(buf, "disable_beacon_ibss=", &val)) {
		if ((val == 1) || (val == 0))
			wifi->params.disable_beacon_ibss = val;
		else
			pr_err("Invalid driver_tput value should be 1 or 0\n");
#ifdef DFS_TEST
	} else if (param_get_val(buf, "radar=", &val)) {
		if (val == 1)
			radar_detected();
		else
			pr_err("Invalid parameter value.\n");
#endif
	} else if (param_get_val(buf, "uccp_debug=", &val)) {
		uccp_debug = val;
	} else
		pr_err("Invalid parameter name: %s\n", buf);
error:
	return count;
}


static int proc_open_config(struct inode *inode, struct file *file)
{
	return single_open(file, proc_read_config, NULL);
}


static int proc_open_phy_stats(struct inode *inode, struct file *file)
{
	return single_open(file, proc_read_phy_stats, NULL);
}


static int proc_open_mac_stats(struct inode *inode, struct file *file)
{
	return single_open(file, proc_read_mac_stats, NULL);
}


static const struct file_operations params_fops_config = {
	.open = proc_open_config,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = proc_write_config,
	.release = single_release
};

static const struct file_operations params_fops_phy_stats = {
	.open = proc_open_phy_stats,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = NULL,
	.release = single_release
};
static const struct file_operations params_fops_mac_stats = {
	.open = proc_open_mac_stats,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = NULL,
	.release = single_release
};
static int proc_init(struct proc_dir_entry ***main_dir_entry)
{
	struct proc_dir_entry *entry;
	int err = 0;
	unsigned int i = 0;
	/*2.4GHz and 5 GHz PD and TX-PWR calibration params*/
	unsigned char rf_params[RF_PARAMS_SIZE * 2];

	strncpy(rf_params,
		"1E00000000002426292A2C2E3237393F454A52576066000000002B2C3033373A3D44474D51575A61656B6F000000002B2C3033373A3D44474D51575A61656B6F000000002B2C3033373A3D44474D51575A61656B6F000000002B2C3033373A3D44474D51575A61656B6F00000000002426292A2C2E3237393F454A52576066000000002B2C3033373A3D44474D51575A61656B6F000000002B2C3033373A3D44474D51575A61656B6F000000002B2C3033373A3D44474D51575A61656B6F000000002B2C3033373A3D44474D51575A61656B6F0808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808080808",
		(RF_PARAMS_SIZE * 2));

	wifi = kzalloc(sizeof(struct wifi_dev), GFP_KERNEL);
	if (!wifi) {
		err = -ENOMEM;
		goto out;
	}

	wifi->umac_proc_dir_entry = proc_mkdir("uccp420", NULL);
	if (!wifi->umac_proc_dir_entry) {
		pr_err("Failed to create proc dir\n");
		err = -ENOMEM;
		goto  proc_dir_fail;
	}

	entry = proc_create("params", 0644, wifi->umac_proc_dir_entry,
			    &params_fops_config);
	if (!entry) {
		pr_err("Failed to create proc entry\n");
		err = -ENOMEM;
		goto  proc_entry1_fail;
	}

	entry = proc_create("phy_stats", 0444, wifi->umac_proc_dir_entry,
			    &params_fops_phy_stats);
	if (!entry) {
		pr_err("Failed to create proc entry\n");
		err = -ENOMEM;
		goto  proc_entry2_fail;
	}

	entry = proc_create("mac_stats", 0444, wifi->umac_proc_dir_entry,
			    &params_fops_mac_stats);
	if (!entry) {
		pr_err("Failed to create proc entry\n");
		err = -ENOMEM;
		goto  proc_entry3_fail;
	}

	/* Initialize WLAN params */
	memset(&wifi->params, 0, sizeof(struct wifi_params));

	/* TODO: Make this a struct */
	memset(wifi->params.rf_params, 0xFF, sizeof(wifi->params.rf_params));
	conv_str_to_byte(wifi->params.rf_params, rf_params, RF_PARAMS_SIZE);

	if (!rf_params_vpd)
		rf_params_vpd = wifi->params.rf_params;

	memcpy(wifi->params.rf_params_vpd, rf_params_vpd, RF_PARAMS_SIZE);

	wifi->params.is_associated = 0;
	wifi->params.ed_sensitivity = -89;
	wifi->params.auto_sensitivity = 1;
	wifi->params.dot11a_support = 1;
	wifi->params.dot11g_support = 1;
	wifi->params.num_vifs = 2;

	/* Check, if required add it */
	wifi->params.tx_fixed_mcs_indx = -1;
	wifi->params.tx_fixed_rate = -1;
	wifi->params.num_spatial_streams = min(MAX_TX_STREAMS, MAX_RX_STREAMS);
	wifi->params.uccp_num_spatial_streams = min(MAX_TX_STREAMS,
						    MAX_RX_STREAMS);
	wifi->params.antenna_sel = 1;

	if (num_streams_vpd > 0)
		wifi->params.uccp_num_spatial_streams = num_streams_vpd;

	wifi->params.bt_state = 1;
	wifi->params.mgd_mode_tx_fixed_mcs_indx = -1;
	wifi->params.mgd_mode_tx_fixed_rate = -1;
	if (vht_support)
		wifi->params.chnl_bw = WLAN_80MHZ_OPERATION;
	else
		wifi->params.chnl_bw = WLAN_20MHZ_OPERATION;
	wifi->params.max_tx_streams = MAX_TX_STREAMS;
	wifi->params.max_rx_streams = MAX_RX_STREAMS;
	wifi->params.max_data_size  = 8 * 1024;

	wifi->params.vht_beamform_enable = VHT_BEAMFORM_DISABLE;
	wifi->params.vht_beamform_period = 1000; /* ms */
	wifi->params.vht_beamform_support = 0;
	if (vht_support)
		wifi->params.max_tx_cmds = MAX_SUBFRAMES_IN_AMPDU_VHT;
	else
		wifi->params.max_tx_cmds = MAX_SUBFRAMES_IN_AMPDU_HT;
	wifi->params.disable_power_save = 0;
	wifi->params.disable_sm_power_save = 0;
	wifi->params.rate_protection_type = 0; /* Disable protection by def */
	wifi->params.prod_mode_rate_preamble_type = 1; /* LONG */
	wifi->params.prod_mode_stbc_enabled = 0;
	wifi->params.prod_mode_bcc_or_ldpc = 0;
	wifi->params.bg_scan_enable = 0;
	memset(wifi->params.bg_scan_channel_list, 0, 50);
	memset(wifi->params.bg_scan_channel_flags, 0, 50);

	if (wifi->params.dot11g_support) {
		wifi->params.bg_scan_num_channels = 3;

		wifi->params.bg_scan_channel_list[i] = 1;
		wifi->params.bg_scan_channel_flags[i++] = ACTIVE;

		wifi->params.bg_scan_channel_list[i] = 6;
		wifi->params.bg_scan_channel_flags[i++] = ACTIVE;

		wifi->params.bg_scan_channel_list[i] = 11;
		wifi->params.bg_scan_channel_flags[i++] = ACTIVE;
	}

	if (wifi->params.dot11a_support) {
		wifi->params.bg_scan_num_channels += 4;

		wifi->params.bg_scan_channel_list[i] = 36;
		wifi->params.bg_scan_channel_flags[i++] = ACTIVE;

		wifi->params.bg_scan_channel_list[i] = 40;
		wifi->params.bg_scan_channel_flags[i++] = ACTIVE;

		wifi->params.bg_scan_channel_list[i] = 44;
		wifi->params.bg_scan_channel_flags[i++] = ACTIVE;

		wifi->params.bg_scan_channel_list[i] = 48;
		wifi->params.bg_scan_channel_flags[i++] = ACTIVE;
	}

	wifi->params.disable_beacon_ibss = 0;
	wifi->params.pkt_gen_val = -1;
	wifi->params.init_pkt_gen = 0;
	wifi->params.payload_length = 4000;
	wifi->params.start_prod_mode = 0;
	wifi->params.init_prod = 0;
	wifi->params.bg_scan_intval = 5000 * 1000; /* Once in 5 seconds */
	wifi->params.bg_scan_chan_dur = 300; /* Channel spending time */
	wifi->params.bg_scan_serv_chan_dur = 100; /* Oper chan spending time */
	wifi->params.nw_selection = 0;
	wifi->params.scan_type = ACTIVE;
	wifi->params.hw_scan_status = HW_SCAN_STATUS_NONE;
	wifi->params.fw_loading = 1;

	**main_dir_entry = wifi->umac_proc_dir_entry;
	return err;

proc_entry3_fail:
	remove_proc_entry("phy_stats", wifi->umac_proc_dir_entry);
proc_entry2_fail:
	remove_proc_entry("params", wifi->umac_proc_dir_entry);
proc_entry1_fail:
	remove_proc_entry("uccp420", NULL);
proc_dir_fail:
	kfree(wifi);
out:
	return err;

}

static void proc_exit(void)
{
	/* This is created in hal_init */
	remove_proc_entry("hal_stats", wifi->umac_proc_dir_entry);
	remove_proc_entry("mac_stats", wifi->umac_proc_dir_entry);
	remove_proc_entry("phy_stats", wifi->umac_proc_dir_entry);
	remove_proc_entry("params", wifi->umac_proc_dir_entry);
	remove_proc_entry("uccp420", NULL);
	kfree(wifi);
}


int _uccp420wlan_80211if_init(struct proc_dir_entry **main_dir_entry)
{
	int error;

	error = proc_init(&main_dir_entry);
	if (error)
		return error;

	error = uccp420wlan_init();

	return error;
}

void _uccp420wlan_80211if_exit(void)
{
	struct mac80211_dev  *dev = (struct mac80211_dev *)wifi->hw->priv;

	if (wifi && wifi->hw) {
		/* We can safely call stop as mac80211
		 * will not call stop because of new
		 * production mode.
		 */
		if (dev && wifi->params.init_prod)
			stop(wifi->hw);
		uccp420wlan_exit();
		proc_exit();
	}
}
