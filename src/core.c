/*
 * File Name  : core.c
 *
 * This file contains the source functions for UMAC core
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

#include <linux/rtnetlink.h>

#include "core.h"

#define UMAC_PRINT(fmt, args...) pr_debug(fmt, ##args)

spinlock_t tsf_lock;

unsigned char bss_addr[6] = {72, 14, 29, 35, 31, 52};
static int is_robust_mgmt(struct sk_buff *skb)
{
	/*TODO: mmie struture not being used now. Uncomment once in use */
#if 0
	struct ieee80211_mmie *mmie;
#endif
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;

	if (skb->len < 24)
		return 0;

	if (!((hdr->frame_control & IEEE80211_FCTL_FTYPE) ==
	      IEEE80211_FTYPE_MGMT))
		return 0;

	/* Not a BIP frame */
	if (((hdr->frame_control & IEEE80211_FCTL_STYPE) ==
	     IEEE80211_STYPE_DISASSOC) ||
	    ((hdr->frame_control & IEEE80211_FCTL_STYPE) ==
	     IEEE80211_STYPE_DEAUTH) ||
	    ((hdr->frame_control & IEEE80211_FCTL_STYPE) ==
	     IEEE80211_STYPE_ACTION)) {
		if (((hdr->frame_control & IEEE80211_FCTL_STYPE) ==
			IEEE80211_STYPE_ACTION)) {
			u8 *category;

			category = ((u8 *) hdr) + 24;

			if (*category == WLAN_CATEGORY_PUBLIC ||
			    *category == WLAN_CATEGORY_HT ||
			    *category == WLAN_CATEGORY_SELF_PROTECTED ||
			    *category == WLAN_CATEGORY_VENDOR_SPECIFIC)
				return 0;
		}
	} else {
		return 0;
	}

	if ((hdr->addr1[0] & 0x01)) {
#if 0
		if (skb->len < (24 + sizeof(*mmie)))
			return 0;

		mmie = (struct ieee80211_mmie *)(skb->data + skb->len -
						 sizeof(*mmie));
		if (mmie->element_id != 76 || mmie->length != sizeof(*mmie) - 2)
			/* #define WLAN_EID_MMIE = 76;*/
			return 0;
#endif
	} else {
		if (hdr->frame_control & IEEE80211_FCTL_PROTECTED)
			return 0;
	}
	return 1;
}


int wait_for_scan_abort(struct mac80211_dev *dev)
{
	int count;

	count = 0;

check_scan_abort_complete:
	if (!dev->scan_abort_done && (count < SCAN_ABORT_TIMEOUT_TICKS)) {
		current->state = TASK_INTERRUPTIBLE;

		if (0 == schedule_timeout(1))
			count++;

		goto check_scan_abort_complete;
	}

	if (!dev->scan_abort_done) {
		UMAC_PRINT("%s-UMAC: No SCAN_ABORT_DONE after %ld ticks\n",
			   dev->name, SCAN_ABORT_TIMEOUT_TICKS);
		return -1;
	}

	UMAC_PRINT("%s-UMAC: Scan abort complete after %d timer ticks\n",
		   dev->name, count);

	return 0;

}


int wait_for_channel_prog_complete(struct mac80211_dev *dev)
{
	int count;

	count = 0;

check_ch_prog_complete:
	if (!dev->chan_prog_done && (count < CH_PROG_TIMEOUT_TICKS)) {
		current->state = TASK_INTERRUPTIBLE;

		if (0 == schedule_timeout(1))
			count++;

		goto check_ch_prog_complete;
	}

	if (!dev->chan_prog_done) {
		UMAC_PRINT("%s-UMAC: No channel prog done after %ld ticks\n",
			   dev->name, CH_PROG_TIMEOUT_TICKS);
		return -1;
	}

	DEBUG_LOG("%s-CORE: Channel Prog Complete after %d timer ticks\n",
		  dev->name, count);

	return 0;

}


int wait_for_reset_complete(struct mac80211_dev *dev)
{
	int count;

	count = 0;

check_reset_complete:
	if (!dev->reset_complete && (count < RESET_TIMEOUT_TICKS)) {
		current->state = TASK_INTERRUPTIBLE;

		if (0 == schedule_timeout(1))
			count++;

		goto check_reset_complete;
	}

	if (!dev->reset_complete) {
		UMAC_PRINT("%s-UMAC: No reset complete after %ld ticks\n",
			   dev->name, RESET_TIMEOUT_TICKS);
		return -1;
	}

	UMAC_PRINT("%s-UMAC: Reset complete after %d timer ticks\n",
		   dev->name, count);
	return 0;

}


#ifdef PERF_PROFILING
static void driver_tput_timer_expiry(unsigned long data)
{
	struct umac_vif *uvif = (struct umac_vif *)data;

	if (uvif->dev->stats->rx_packet_data_count) {
		pr_info("The RX packets/sec are: %d\n",
			uvif->dev->stats->rx_packet_data_count);

		uvif->dev->stats->rx_packet_data_count = 0;
	}

	if (uvif->dev->stats->tx_cmd_send_count_single) {
		pr_info("The TX packets/sec single are: %d\n",
			uvif->dev->stats->tx_cmd_send_count_single);

		uvif->dev->stats->tx_cmd_send_count_single = 0;
	}

	if (uvif->dev->stats->tx_cmd_send_count_multi) {
		pr_info("The TX packets/sec multi are: %d\n",
			uvif->dev->stats->tx_cmd_send_count_multi);

		uvif->dev->stats->tx_cmd_send_count_multi = 0;
	}

	mod_timer(&uvif->driver_tput_timer, jiffies + msecs_to_jiffies(1000));

}
#endif

void proc_bss_info_changed(unsigned char *mac_addr, int value)
{
		int temp = 0, i = 0, j = 0;

		get_random_bytes(&j, sizeof(j));
		for (i = 5; i > 0; i--) {
			j = j % (i+1);
			temp = bss_addr[i];
			bss_addr[i] = bss_addr[j];
			bss_addr[j] = temp;
			}
		uccp420wlan_prog_vif_bssid(0, mac_addr, bss_addr);

}

void packet_generation(unsigned long data)
{
		struct mac80211_dev *dev = (struct mac80211_dev *)data;
		unsigned char *mac_addr = dev->if_mac_addresses[0].addr;
		struct ieee80211_hdr hdr = {0};
		struct sk_buff *skb;
		unsigned char broad_addr[6] = {0xff, 0xff, 0xff,
					       0xff, 0xff, 0xff};
		u16 hdrlen = 26;

		/*LOOP_START*/
		/*PREPARE_SKB_LIST and SEND*/

		skb = alloc_skb(dev->params->payload_length + hdrlen,
				GFP_ATOMIC);
		ether_addr_copy(hdr.addr1, broad_addr);
		ether_addr_copy(hdr.addr2, mac_addr);
		ether_addr_copy(hdr.addr3, bss_addr);
		hdr.frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
						IEEE80211_STYPE_QOS_DATA);
		memcpy(skb_put(skb, hdrlen), &hdr, hdrlen);
		memset(skb_put(skb, dev->params->payload_length), 0xAB,
			dev->params->payload_length);

		/*LOOP_END*/
		skb_queue_tail(&dev->tx.proc_tx_list[0], skb);
		uccp420wlan_proc_tx();

}

static void vif_bcn_timer_expiry(unsigned long data)
{
	struct umac_vif *uvif = (struct umac_vif *)data;
	struct sk_buff *skb, *temp;
	struct sk_buff_head bcast_frames;
	unsigned long flags;

	if (uvif->vif->bss_conf.enable_beacon == false)
		return;

	if (uvif->vif->type == NL80211_IFTYPE_AP) {
		temp = skb = ieee80211_beacon_get(uvif->dev->hw, uvif->vif);

		if (!skb) {
			/* No beacon, so dont transmit braodcast frames*/
			goto reschedule_timer;
		}

		skb_queue_head_init(&bcast_frames);
		skb->priority = 1;
		skb_queue_tail(&bcast_frames, skb);

		skb = ieee80211_get_buffered_bc(uvif->dev->hw, uvif->vif);

		while (skb) {
			/* Hack: skb->priority is used to indicate more
			 * frames
			 */
			skb->priority = 1;
			skb_queue_tail(&bcast_frames, skb);
			temp = skb;
			skb = ieee80211_get_buffered_bc(uvif->dev->hw,
							uvif->vif);
		}

		if (temp)
			temp->priority = 0;

		spin_lock_irqsave(&uvif->dev->bcast_lock, flags);

		while ((skb = skb_dequeue(&bcast_frames)))
			uccp420wlan_tx_frame(skb, NULL, uvif->dev, true);

		spin_unlock_irqrestore(&uvif->dev->bcast_lock, flags);

	} else {
		skb = ieee80211_beacon_get(uvif->dev->hw, uvif->vif);

		if (!skb)
			goto reschedule_timer;

		uccp420wlan_tx_frame(skb, NULL, uvif->dev, true);

	}
reschedule_timer:
	return;

}


int uccp420wlan_core_init(struct mac80211_dev *dev, unsigned int ftm)
{

	DEBUG_LOG("%s-CORE: Init called\n", dev->name);
	spin_lock_init(&tsf_lock);
	uccp420wlan_lmac_if_init(dev, dev->name);

	/* Enable the LMAC, set defaults and initialize TX */
	dev->reset_complete = 0;

	UMAC_PRINT("%s-UMAC: Reset (ENABLE)\n", dev->name);

	if (hal_ops.start(dev->umac_proc_dir_entry))
		goto lmac_deinit;
	if (ftm)
		uccp420wlan_prog_reset(LMAC_ENABLE, LMAC_MODE_FTM);
	else
		uccp420wlan_prog_reset(LMAC_ENABLE, LMAC_MODE_NORMAL);

	if (wait_for_reset_complete(dev) < 0)
		goto hal_stop;

	if (hal_ops.init_bufs(NUM_TX_DESCS,
			      NUM_RX_BUFS_2K,
			      NUM_RX_BUFS_12K,
			      dev->params->max_data_size) < 0)
		goto hal_stop;

	uccp420wlan_prog_btinfo(dev->params->bt_state);
	uccp420wlan_prog_global_cfg(512, /* Rx MSDU life time in msecs */
				    512, /* Tx MSDU life time in msecs */
				    dev->params->ed_sensitivity,
				    dev->params->auto_sensitivity,
				    dev->params->rf_params);

	uccp420wlan_prog_txpower(dev->txpower);
	uccp420wlan_tx_init(dev);

	return 0;
hal_stop:
	hal_ops.stop(dev->umac_proc_dir_entry);
lmac_deinit:
	uccp420wlan_lmac_if_deinit();
	return -1;
}


void uccp420wlan_core_deinit(struct mac80211_dev *dev, unsigned int ftm)
{
	DEBUG_LOG("%s-CORE: De-init called\n", dev->name);

	/* De initialize tx  and disable LMAC*/
	uccp420wlan_tx_deinit(dev);

	/* Disable the LMAC */
	dev->reset_complete = 0;
	UMAC_PRINT("%s-UMAC: Reset (DISABLE)\n", dev->name);

	if (ftm)
		uccp420wlan_prog_reset(LMAC_DISABLE, LMAC_MODE_FTM);
	else
		uccp420wlan_prog_reset(LMAC_DISABLE, LMAC_MODE_NORMAL);


	wait_for_reset_complete(dev);

	uccp420_lmac_if_free_outstnding();

	hal_ops.stop(dev->umac_proc_dir_entry);
	hal_ops.deinit_bufs();

	uccp420wlan_lmac_if_deinit();
}


void uccp420wlan_vif_add(struct umac_vif *uvif)
{
	unsigned int type;
	struct ieee80211_conf *conf = &uvif->dev->hw->conf;

	DEBUG_LOG("%s-CORE: Add VIF %d Type = %d\n",
		   uvif->dev->name, uvif->vif_index, uvif->vif->type);

	uvif->config.atim_window = uvif->config.bcn_lost_cnt =
		uvif->config.aid = 0;

	switch (uvif->vif->type) {
	case NL80211_IFTYPE_STATION:
		type = IF_MODE_STA_BSS;
		uvif->noa_active = 0;
		skb_queue_head_init(&uvif->noa_que);
		spin_lock_init(&uvif->noa_que.lock);
		break;
	case NL80211_IFTYPE_ADHOC:
		type = IF_MODE_STA_IBSS;
		init_timer(&uvif->bcn_timer);
		uvif->bcn_timer.data = (unsigned long)uvif;
		uvif->bcn_timer.function = vif_bcn_timer_expiry;
		spin_lock_init(&uvif->noa_que.lock);
		break;
	case NL80211_IFTYPE_AP:
		type = IF_MODE_AP;
		init_timer(&uvif->bcn_timer);
		uvif->bcn_timer.data = (unsigned long)uvif;
		uvif->bcn_timer.function = vif_bcn_timer_expiry;
		spin_lock_init(&uvif->noa_que.lock);
		break;
	default:
		WARN_ON(1);
		return;
	}

#ifdef PERF_PROFILING
	/* Timer to print stats for tput*/
	init_timer(&uvif->driver_tput_timer);
	uvif->driver_tput_timer.data = (unsigned long)uvif;
	uvif->driver_tput_timer.function = driver_tput_timer_expiry;
#endif
	uccp420wlan_prog_vif_ctrl(uvif->vif_index,
				  uvif->vif->addr,
				  type,
				  IF_ADD);

	/* Reprogram retry counts */
	uccp420wlan_prog_short_retry(uvif->vif_index, uvif->vif->addr,
					 conf->short_frame_max_tx_count);

	uccp420wlan_prog_long_retry(uvif->vif_index, uvif->vif->addr,
					conf->long_frame_max_tx_count);

	if (uvif->vif->type == NL80211_IFTYPE_AP) {
		/* Program the EDCA params */
		unsigned int queue;
		unsigned int aifs;
		unsigned int txop;
		unsigned int cwmin;
		unsigned int cwmax;
		unsigned int uapsd;

		for (queue = 0; queue < 4; queue++) {
			aifs = uvif->config.edca_params[queue].aifs;
			txop = uvif->config.edca_params[queue].txop;
			cwmin = uvif->config.edca_params[queue].cwmin;
			cwmax = uvif->config.edca_params[queue].cwmax;
			uapsd = uvif->config.edca_params[queue].uapsd;

			uccp420wlan_prog_txq_params(uvif->vif_index,
						    uvif->vif->addr,
						    queue,
						    aifs,
						    txop,
						    cwmin,
						    cwmax,
						    uapsd);
		}
	}
}


void uccp420wlan_vif_remove(struct umac_vif *uvif)
{
	struct sk_buff *skb;
	unsigned int type;
	unsigned long flags;

	DEBUG_LOG("%s-CORE: Remove VIF %d called\n", uvif->dev->name,
		   uvif->vif_index);

	switch (uvif->vif->type) {
	case NL80211_IFTYPE_STATION:
		type = IF_MODE_STA_BSS;
		break;
	case NL80211_IFTYPE_ADHOC:
		type = IF_MODE_STA_IBSS;
		del_timer(&uvif->bcn_timer);
		break;
	case NL80211_IFTYPE_AP:
		type = IF_MODE_AP;
		del_timer(&uvif->bcn_timer);
		break;
	default:
		WARN_ON(1);
		return;
	}

#ifdef PERF_PROFILING
	del_timer(&uvif->driver_tput_timer);
#endif

	spin_lock_irqsave(&uvif->noa_que.lock, flags);

	while ((skb = __skb_dequeue(&uvif->noa_que)))
		dev_kfree_skb(skb);

	spin_unlock_irqrestore(&uvif->noa_que.lock, flags);

	uccp420wlan_prog_vif_ctrl(uvif->vif_index,
				  uvif->vif->addr,
				  type,
				  IF_REM);

}


void uccp420wlan_vif_set_edca_params(unsigned short queue,
				     struct umac_vif *uvif,
				     struct edca_params *params,
				     unsigned int vif_active)
{
	switch (queue) {
	case 0:
		queue = 3; /* Voice */
		break;
	case 1:
		queue = 2; /* Video */
		break;
	case 2:
		queue = 1; /* Best effort */
		break;
	case 3:
		queue = 0; /* Back groud */
		break;
	}

	DEBUG_LOG("%s-CORE:Set EDCA params, VIF %d, Val: %d, %d, %d, %d, %d\n",
		   uvif->dev ? uvif->dev->name : 0, uvif->vif_index, queue,
		   params->aifs, params->txop, params->cwmin, params->cwmax);

	if (uvif->dev->params->production_test == 0) {
		/* arbitration interframe space [0..255] */
		uvif->config.edca_params[queue].aifs = params->aifs;

		/* maximum burst time in units of 32 usecs, 0 meaning disabled*/
		uvif->config.edca_params[queue].txop = params->txop;

		/* minimum contention window in units of  2^n-1 */
		uvif->config.edca_params[queue].cwmin = params->cwmin;

		/*  maximum contention window in units of 2^n-1 */
		uvif->config.edca_params[queue].cwmax = params->cwmax;
		uvif->config.edca_params[queue].uapsd = params->uapsd;
	} else {
		uvif->config.edca_params[queue].aifs = 3;
		uvif->config.edca_params[queue].txop = 0;
		uvif->config.edca_params[queue].cwmin = 0;
		uvif->config.edca_params[queue].cwmax = 0;
		uvif->config.edca_params[queue].uapsd = 0;
	}

	/* For the AP case, EDCA params are set before ADD interface is called.
	 * Since this is not supported, we simply store the params and program
	 * them to the LMAC after the interface is added
	 */
	if (!vif_active)
		return;

	/* Program the txq parameters into the LMAC */
	uccp420wlan_prog_txq_params(uvif->vif_index,
				    uvif->vif->addr,
				    queue,
				    params->aifs,
				    params->txop,
				    params->cwmin,
				    params->cwmax,
				    params->uapsd);

}


void uccp420wlan_vif_bss_info_changed(struct umac_vif *uvif,
				      struct ieee80211_bss_conf *bss_conf,
				      unsigned int changed)
{
	unsigned int bcn_int = 0;
	unsigned long bcn_tim_val = 0;
	unsigned int caps = 0;
	int center_freq = 0;
	int chan = 0;
	unsigned int bform_enable = 0;
	unsigned int bform_per = 0;

	DEBUG_LOG("%s-CORE: BSS INFO changed %d, %d, %d\n", uvif->dev->name,
		   uvif->vif_index, uvif->vif->type, changed);


	if (changed & BSS_CHANGED_BSSID)
		uccp420wlan_prog_vif_bssid(uvif->vif_index, uvif->vif->addr,
					   (unsigned char *)bss_conf->bssid);

	if (changed & BSS_CHANGED_BASIC_RATES) {
		if (bss_conf->basic_rates)
			uccp420wlan_prog_vif_basic_rates(uvif->vif_index,
							 uvif->vif->addr,
							 bss_conf->basic_rates);
		else
			uccp420wlan_prog_vif_basic_rates(uvif->vif_index,
							 uvif->vif->addr,
							 0x153);
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		unsigned int queue = 0;
		unsigned int aifs = 0;
		unsigned int txop = 0;
		unsigned int cwmin = 0;
		unsigned int cwmax = 0;
		unsigned int uapsd = 0;

		uccp420wlan_prog_vif_short_slot(uvif->vif_index,
						uvif->vif->addr,
						bss_conf->use_short_slot);

		for (queue = 0; queue < WLAN_AC_MAX_CNT; queue++) {
			aifs = uvif->config.edca_params[queue].aifs;
			txop = uvif->config.edca_params[queue].txop;
			cwmin = uvif->config.edca_params[queue].cwmin;
			cwmax = uvif->config.edca_params[queue].cwmax;
			uapsd = uvif->config.edca_params[queue].uapsd;

			if (uvif->config.edca_params[queue].cwmin != 0)
				uccp420wlan_prog_txq_params(uvif->vif_index,
							    uvif->vif->addr,
							    queue,
							    aifs,
							    txop,
							    cwmin,
							    cwmax,
							    uapsd);
		}
	}

	switch (uvif->vif->type) {
	case NL80211_IFTYPE_STATION:
		if (changed & BSS_CHANGED_ASSOC) {
			center_freq = bss_conf->chandef.chan->center_freq;
			chan = ieee80211_frequency_to_channel(center_freq);
			bform_enable = uvif->dev->params->vht_beamform_enable;
			bform_per = uvif->dev->params->vht_beamform_period;

			if (bss_conf->assoc) {
				DEBUG_LOG("%s-CORE: AID %d, CAPS 0x%04x\n",
					   uvif->dev->name, bss_conf->aid,
					   bss_conf->assoc_capability |
					   (bss_conf->qos << 9));

				uccp420wlan_prog_vif_conn_state(uvif->vif_index,
								uvif->vif->addr,
								STA_CONN);

				uccp420wlan_prog_vif_aid(uvif->vif_index,
							 uvif->vif->addr,
							 bss_conf->aid);

				uccp420wlan_prog_vif_op_channel(uvif->vif_index,
								uvif->vif->addr,
								chan);

				caps = (bss_conf->assoc_capability |
					(bss_conf->qos << 9));

				uccp420wlan_prog_vif_assoc_cap(uvif->vif_index,
							       uvif->vif->addr,
							       caps);

				if (uvif->dev->params->vht_beamform_support)
					uccp420wlan_prog_vht_bform(bform_enable,
								   bform_per);

				uvif->noa_active = 0;
				uvif->dev->params->is_associated = 1;

#ifdef PERF_PROFILING
				mod_timer(&uvif->driver_tput_timer,
					  jiffies + msecs_to_jiffies(1000));
#endif
			} else {
				uvif->dev->params->is_associated = 0;

				uccp420wlan_prog_vif_conn_state(uvif->vif_index,
								uvif->vif->addr,
								STA_DISCONN);

				uccp420wlan_prog_vht_bform(VHT_BEAMFORM_DISABLE,
							   bform_per);

			}
		}

		if (changed & BSS_CHANGED_BEACON_INT) {
			uccp420wlan_prog_vif_beacon_int(uvif->vif_index,
							uvif->vif->addr,
							bss_conf->beacon_int);

		}

		if (changed & BSS_CHANGED_BEACON_INFO) {
			uccp420wlan_prog_vif_dtim_period(uvif->vif_index,
							 uvif->vif->addr,
							 bss_conf->dtim_period);

		}

		break;
	case NL80211_IFTYPE_ADHOC:
		if (changed & BSS_CHANGED_BEACON_ENABLED) {
			if (uvif->vif->bss_conf.enable_beacon == true) {

				bcn_int = bss_conf->beacon_int;
				bcn_tim_val =  msecs_to_jiffies(bcn_int - 10);

				mod_timer(&uvif->bcn_timer,
					  jiffies + bcn_tim_val);
			} else {
				del_timer(&uvif->bcn_timer);
			}
		}

		if (changed & BSS_CHANGED_BEACON_INT) {
			bcn_int = bss_conf->beacon_int;
			bcn_tim_val =  msecs_to_jiffies(bcn_int - 10);

			if (uvif->vif->bss_conf.enable_beacon == true) {
				mod_timer(&uvif->bcn_timer,
					  jiffies + bcn_tim_val);

				uccp420wlan_prog_vif_beacon_int(uvif->vif_index,
								uvif->vif->addr,
								bcn_int);
			}
		}

		break;
	case NL80211_IFTYPE_AP:
		if (changed & BSS_CHANGED_BEACON_ENABLED) {
			if (uvif->vif->bss_conf.enable_beacon == true) {
				bcn_int = uvif->vif->bss_conf.beacon_int;
				bcn_tim_val =  msecs_to_jiffies(bcn_int - 10);

				mod_timer(&uvif->bcn_timer,
					  jiffies + bcn_tim_val);

			} else {
				del_timer(&uvif->bcn_timer);
			}
		}

		if (changed & BSS_CHANGED_BEACON_INT) {
			bcn_int = bss_conf->beacon_int;
			bcn_tim_val =  msecs_to_jiffies(bcn_int - 10);

			if (uvif->vif->bss_conf.enable_beacon == true) {
				mod_timer(&uvif->bcn_timer,
					  jiffies + bcn_tim_val);

				uccp420wlan_prog_vif_beacon_int(uvif->vif_index,
								uvif->vif->addr,
								bcn_int);
			}
		}

		break;
	default:
		WARN_ON(1);
		return;
	}

}


void uccp420wlan_reset_complete(char *lmac_version, void *context)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)context;

	memcpy(dev->stats->uccp420_lmac_version, lmac_version, 5);
	dev->stats->uccp420_lmac_version[5] = '\0';
	dev->reset_complete = 1;
}


void uccp420wlan_mib_stats(struct umac_event_mib_stats *mib_stats,
			   void *context)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)context;

	dev->stats->ed_cnt = mib_stats->ed_cnt;
	dev->stats->mpdu_cnt = mib_stats->mpdu_cnt;
	dev->stats->ofdm_crc32_pass_cnt = mib_stats->ofdm_crc32_pass_cnt;
	dev->stats->ofdm_crc32_fail_cnt = mib_stats->ofdm_crc32_fail_cnt;
	dev->stats->dsss_crc32_pass_cnt = mib_stats->dsss_crc32_pass_cnt;
	dev->stats->dsss_crc32_fail_cnt = mib_stats->dsss_crc32_fail_cnt;
	dev->stats->mac_id_pass_cnt = mib_stats->mac_id_pass_cnt;
	dev->stats->mac_id_fail_cnt = mib_stats->mac_id_fail_cnt;
	dev->stats->ofdm_corr_pass_cnt = mib_stats->ofdm_corr_pass_cnt;
	dev->stats->ofdm_corr_fail_cnt = mib_stats->ofdm_corr_fail_cnt;
	dev->stats->dsss_corr_pass_cnt = mib_stats->dsss_corr_pass_cnt;
	dev->stats->dsss_corr_fail_cnt = mib_stats->dsss_corr_fail_cnt;
	dev->stats->ofdm_s2l_fail_cnt = mib_stats->ofdm_s2l_fail_cnt;
	dev->stats->lsig_fail_cnt = mib_stats->lsig_fail_cnt;
	dev->stats->htsig_fail_cnt = mib_stats->htsig_fail_cnt;
	dev->stats->vhtsiga_fail_cnt = mib_stats->vhtsiga_fail_cnt;
	dev->stats->vhtsigb_fail_cnt = mib_stats->vhtsigb_fail_cnt;
	dev->stats->nonht_ofdm_cnt = mib_stats->nonht_ofdm_cnt;
	dev->stats->nonht_dsss_cnt = mib_stats->nonht_dsss_cnt;
	dev->stats->mm_cnt = mib_stats->mm_cnt;
	dev->stats->gf_cnt = mib_stats->gf_cnt;
	dev->stats->vht_cnt = mib_stats->vht_cnt;
	dev->stats->aggregation_cnt = mib_stats->aggregation_cnt;
	dev->stats->non_aggregation_cnt = mib_stats->non_aggregation_cnt;
	dev->stats->ndp_cnt = mib_stats->ndp_cnt;
	dev->stats->ofdm_ldpc_cnt = mib_stats->ofdm_ldpc_cnt;
	dev->stats->ofdm_bcc_cnt = mib_stats->ofdm_bcc_cnt;
	dev->stats->midpacket_cnt = mib_stats->midpacket_cnt;
	dev->stats->dsss_sfd_fail_cnt = mib_stats->dsss_sfd_fail_cnt;
	dev->stats->dsss_hdr_fail_cnt = mib_stats->dsss_hdr_fail_cnt;
	dev->stats->dsss_short_preamble_cnt =
		mib_stats->dsss_short_preamble_cnt;
	dev->stats->dsss_long_preamble_cnt = mib_stats->dsss_long_preamble_cnt;
	dev->stats->sifs_event_cnt = mib_stats->sifs_event_cnt;
	dev->stats->cts_cnt = mib_stats->cts_cnt;
	dev->stats->ack_cnt = mib_stats->ack_cnt;
	dev->stats->sifs_no_resp_cnt = mib_stats->sifs_no_resp_cnt;
	dev->stats->unsupported_cnt = mib_stats->unsupported_cnt;
	dev->stats->l1_corr_fail_cnt = mib_stats->l1_corr_fail_cnt;
	dev->stats->phy_stats_reserved22 = mib_stats->phy_stats_reserved22;
	dev->stats->phy_stats_reserved23 = mib_stats->phy_stats_reserved23;
	dev->stats->phy_stats_reserved24 = mib_stats->phy_stats_reserved24;
	dev->stats->phy_stats_reserved25 = mib_stats->phy_stats_reserved25;
	dev->stats->phy_stats_reserved26 = mib_stats->phy_stats_reserved26;
	dev->stats->phy_stats_reserved27 = mib_stats->phy_stats_reserved27;
	dev->stats->phy_stats_reserved28 = mib_stats->phy_stats_reserved28;
	dev->stats->phy_stats_reserved29 = mib_stats->phy_stats_reserved29;
	dev->stats->phy_stats_reserved30 = mib_stats->phy_stats_reserved30;

}

void uccp420wlan_mac_stats(struct umac_event_mac_stats *mac_stats,
			   void *context)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)context;

	/* TX related */
	dev->stats->tx_cmd_cnt = mac_stats->tx_cmd_cnt;
	dev->stats->tx_done_cnt = mac_stats->tx_done_cnt;
	dev->stats->tx_edca_trigger_cnt = mac_stats->tx_edca_trigger_cnt;
	dev->stats->tx_edca_isr_cnt = mac_stats->tx_edca_isr_cnt;
	dev->stats->tx_start_cnt = mac_stats->tx_start_cnt;
	dev->stats->tx_abort_cnt = mac_stats->tx_abort_cnt;
	dev->stats->tx_abort_isr_cnt = mac_stats->tx_abort_isr_cnt;
	dev->stats->tx_underrun_cnt = mac_stats->tx_underrun_cnt;
	dev->stats->tx_rts_cnt = mac_stats->tx_rts_cnt;
	dev->stats->tx_ampdu_cnt = mac_stats->tx_ampdu_cnt;
	dev->stats->tx_mpdu_cnt = mac_stats->tx_mpdu_cnt;

	/* RX related */
	dev->stats->rx_isr_cnt = mac_stats->rx_isr_cnt;
	dev->stats->rx_ack_cts_to_cnt = mac_stats->rx_ack_cts_to_cnt;
	dev->stats->rx_cts_cnt = mac_stats->rx_cts_cnt;
	dev->stats->rx_ack_resp_cnt = mac_stats->rx_ack_resp_cnt;
	dev->stats->rx_ba_resp_cnt = mac_stats->rx_ba_resp_cnt;
	dev->stats->rx_fail_in_ba_bitmap_cnt =
		mac_stats->rx_fail_in_ba_bitmap_cnt;
	dev->stats->rx_circular_buffer_free_cnt =
		mac_stats->rx_circular_buffer_free_cnt;
	dev->stats->rx_mic_fail_cnt = mac_stats->rx_mic_fail_cnt;

	/* HAL related */
	dev->stats->hal_cmd_cnt = mac_stats->hal_cmd_cnt;
	dev->stats->hal_event_cnt = mac_stats->hal_event_cnt;
	dev->stats->hal_ext_ptr_null_cnt = mac_stats->hal_ext_ptr_null_cnt;
}
void uccp420wlan_rf_calib_data(struct umac_event_rf_calib_data *rf_data,
			       void *context)
{
	struct mac80211_dev  *dev = (struct mac80211_dev *)context;

	if (rf_data->rf_calib_data_length > MAX_RF_CALIB_DATA) {
		printk_once("%s: RF calib data exceeded the max size: %d\n",
			    __func__,
			    MAX_RF_CALIB_DATA);
		return;
	}
	dev->stats->rf_calib_data_length = rf_data->rf_calib_data_length;
	memset(dev->stats->rf_calib_data, 0x00,
	       MAX_RF_CALIB_DATA);
	memcpy(dev->stats->rf_calib_data, rf_data->rf_calib_data,
	       rf_data->rf_calib_data_length);
}
void uccp420wlan_noa_event(int event, struct umac_event_noa *noa, void *context,
			   struct sk_buff *skb)
{
	struct mac80211_dev  *dev = (struct mac80211_dev *)context;
	struct ieee80211_vif *vif;
	struct umac_vif *uvif;
	unsigned long flags;
	bool transmit = false;

	rcu_read_lock();

	vif = (struct ieee80211_vif *)rcu_dereference(dev->vifs[noa->if_index]);

	if (vif == NULL) {
		rcu_read_unlock();
		return;
	}

	uvif = (struct umac_vif *)vif->drv_priv;

	spin_lock_irqsave(&uvif->noa_que.lock, flags);

	if (event == FROM_TX) {
		if (uvif->noa_active) {
			if (!uvif->noa_tx_allowed || skb_peek(&uvif->noa_que))
				__skb_queue_tail(&uvif->noa_que, skb);
			else
				transmit = true;
		} else
			transmit = true;
	} else if (event == FROM_TX_DONE) {
		if (uvif->noa_active && uvif->noa_tx_allowed) {
			skb = __skb_dequeue(&uvif->noa_que);

			if (skb)
				transmit = true;
		}
	} else { /* event = FROM_EVENT_NOA */

		uvif->noa_active = noa->noa_active;

		if (uvif->noa_active) {
			pr_debug("%s: noa active = %d, ap_present = %d\n",
				 dev->name, noa->noa_active, noa->ap_present);

			uvif->noa_tx_allowed = noa->ap_present;

			if (uvif->noa_tx_allowed) {
				skb = __skb_dequeue(&uvif->noa_que);
				if (skb)
					transmit = true;
			}
		} else {
			pr_debug("%s: noa active = %d\n",
				 dev->name, noa->noa_active);

			uvif->noa_tx_allowed = 1;

			/* Can be done in a better way. For now, just flush the
			 * NoA Queue
			 */
			while ((skb = __skb_dequeue(&uvif->noa_que)))
				dev_kfree_skb_any(skb);
		}
	}

	spin_unlock_irqrestore(&uvif->noa_que.lock, flags);

	rcu_read_unlock();

	if (transmit)
		uccp420wlan_tx_frame(skb, NULL, dev, false);
}

#if 0
/* Beacon Time Stamp */
static unsigned int get_real_ts2(unsigned int t2, unsigned int delta)
{
	unsigned int td = 0;
	unsigned int clocks = 0;
	unsigned int clock_mask = 0, tck_num = 0, tck_denom = 0;

	if (get_evt_timer_freq) {
		get_evt_timer_freq(&clock_mask, &tck_num, &tck_denom);
	} else {
		clock_mask = CLOCK_MASK;
		tck_num = TICK_NUMRATOR;
		tck_denom = TICK_DENOMINATOR;
	}

	clocks = delta * tck_num;
	/* clocks = clocks / tck_denom; */
	do_div(clocks, tck_denom);

	if (t2 >= clocks)
		td = t2 - clocks;
	else
		td = clock_mask + (t2 + 1) - clocks;

	return td & clock_mask;
}
#endif


#ifdef MULTI_CHAN_SUPPORT
void uccp420wlan_proc_ch_sw_event(struct umac_event_ch_switch *ch_sw_info,
				  void *context)
{
	struct mac80211_dev *dev = NULL;
	int chan = 0;
	int curr_freq = 0;
	int chan_id = 0;
	struct sk_buff_head *txq = NULL;
	int txq_len = 0;
	int i = 0;
	int queue = 0;
	unsigned long flags = 0;
	int curr_bit = 0;
	int pool_id = 0;
	int ret = 0;
#ifdef UNIFORM_BW_SHARING
	int peer_id = -1;
#else
	int pkts_pend = 0;
#endif
	int ac = 0;
	struct ieee80211_chanctx_conf *curr_chanctx = NULL;
	struct tx_config *tx = NULL;

	if (!ch_sw_info || !context) {
		pr_err("%s: Invalid Parameters\n", __func__);
		return;
	}

	dev = (struct mac80211_dev *)context;
	chan = ch_sw_info->chan;
	tx = &dev->tx;

	rcu_read_lock();

	for (i = 0; i < MAX_CHANCTX; i++) {
		curr_chanctx = rcu_dereference(dev->chanctx[i]);

		if (curr_chanctx) {
			curr_freq = curr_chanctx->def.chan->center_freq;

			if (ieee80211_frequency_to_channel(curr_freq) == chan) {
				chan_id = i;
				break;
			}
		}
	}

	rcu_read_unlock();

	if (i == MAX_CHANCTX) {
		pr_err("%s: Invalid Channel Context\n", __func__);
		return;
	}


	/* Switch to the new channel context */
	/* SDK: Take care of locking requirements for these elements */
	dev->curr_chanctx_idx = chan_id;

	/* We now try to xmit any frames whose xmission got cancelled due to a
	 * previous channel switch */
	for (i = 0; i < NUM_TX_DESCS; i++) {
		spin_lock_irqsave(&tx->lock, flags);

		curr_bit = (i % TX_DESC_BUCKET_BOUND);
		pool_id = (i / TX_DESC_BUCKET_BOUND);

		if (test_and_set_bit(curr_bit, &tx->buf_pool_bmp[pool_id])) {
			spin_unlock_irqrestore(&tx->lock, flags);
			continue;
		}


		txq = &tx->pkt_info[dev->curr_chanctx_idx][i].pkt;
		txq_len = skb_queue_len(txq);
		queue = tx->pkt_info[dev->curr_chanctx_idx][i].queue;

		if (!txq_len) {
			/* Reserved token */
			if (i < (NUM_TX_DESCS_PER_AC * NUM_ACS)) {
				queue = (i % NUM_ACS);
#ifdef UNIFORM_BW_SHARING
				peer_id = get_curr_peer_opp(dev, queue);

				if (peer_id == -1) {
#else
				pkts_pend =
					skb_queue_len(&tx->pending_pkt[queue]);

				if (!pkts_pend) {
#endif
					/* Mark the token as available */
					__clear_bit(curr_bit,
						    &tx->buf_pool_bmp[pool_id]);

					spin_unlock_irqrestore(&tx->lock,
							       flags);
					continue;
				}

			/* Spare token */
			} else {
				for (ac = WLAN_AC_VO; ac >= 0; ac--) {
#ifdef UNIFORM_BW_SHARING
					peer_id = get_curr_peer_opp(dev, ac);

					if (peer_id != -1) {
#else
					pkts_pend =
					   skb_queue_len(&tx->pending_pkt[ac]);

					if (pkts_pend) {
#endif
						queue = ac;
						break;
					}
				}

				if (ac < 0) {

					/* Mark the token as available */
					__clear_bit(curr_bit,
						    &tx->buf_pool_bmp[pool_id]);

					spin_unlock_irqrestore(&tx->lock,
							       flags);
					continue;
				}
			}

			uccp420wlan_tx_proc_pend_frms(dev,
						      queue,
#ifdef UNIFORM_BW_SHARING
						      peer_id,
#endif
						      i);

			tx->outstanding_tokens[queue]++;

		}

		spin_unlock_irqrestore(&tx->lock, flags);

		ret = __uccp420wlan_tx_frame(dev,
					     queue,
					     i,
					     0); /* TODO: Currently sending 0
						    since this param is not used
						    as expected in the orig
						    code for multiple frames etc
						    Need to set this
						    properly when the orig code
						    logic is corrected */
		if (ret < 0) {
			/* SDK: Check if we need to clear the TX bitmap and
			 * desc_chan_map here */
			pr_err("%s: Queueing of TX frame to FW failed\n",
			       __func__);
		} else {
			spin_lock_irqsave(&tx->lock, flags);
			tx->desc_chan_map[i] = dev->curr_chanctx_idx;
			spin_unlock_irqrestore(&tx->lock, flags);
		}

	}
}
#endif


void uccp420wlan_rx_frame(struct sk_buff *skb, void *context)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)context;
	struct wlan_rx_pkt *rx = (struct wlan_rx_pkt *)(skb->data);
	struct ieee80211_hdr *hdr;
	struct ieee80211_rx_status rx_status;
	struct ieee80211_supported_band *band = NULL;
	int i;
	static unsigned int rssi_index;
	struct ieee80211_vif *vif = NULL;

	/* Remove RX control information:
	 * unused more_cmd_data in RX direction is used to indicate QoS/Non-Qos
	 * frames
	 */
	/*pr_debug(" more command : %d\n", rx->hdr.more_cmd_data);*/
	if (rx->hdr.more_cmd_data == 0) {
		/* Non-QOS case*/
		skb_pull(skb, sizeof(struct wlan_rx_pkt));
	} else {
		/* Qos Case: The UCCP overwrites the 2 reserved bytes with data
		 * to maintain the 4 byte alignment of total length and 2 byte
		 * alignment
		 * of starting address (as expected by mac80211).
		 */
		skb_pull(skb, sizeof(struct wlan_rx_pkt) - 2);
		skb_trim(skb, skb->len - 2);
	}

#ifdef DRIVER_DEBUG
	pr_debug("%s-RX: RX frame, Len = %d, RSSI = %d, Rate = %d\n",
		 dev->name, rx->pkt_length, rx->rssi, rx->rate_or_mcs);
	/* print_hex_dump(KERN_DEBUG, " ", DUMP_PREFIX_NONE, 16 ,1, skb->data,
	 * skb->len,1);
	 */
#endif

	hdr = (struct ieee80211_hdr *)skb->data;

	/* Stats for debugging */
	if (ieee80211_is_data(hdr->frame_control)) {
		dev->stats->rx_packet_data_count++;

#ifdef PERF_PROFILING
		if (dev->params->driver_tput == 1) {
			dev_kfree_skb_any(skb);
			return;
		}
#endif
	} else if (ieee80211_is_mgmt(hdr->frame_control)) {
		dev->stats->rx_packet_mgmt_count++;
	}

	memset(&rx_status, 0, sizeof(struct ieee80211_rx_status));

	if (rx->channel < 15)
		rx_status.band = IEEE80211_BAND_2GHZ;
	else
		rx_status.band = IEEE80211_BAND_5GHZ;

	rx_status.freq = ieee80211_channel_to_frequency(rx->channel,
							rx_status.band);
	rx_status.signal = rx->rssi;

	/* RSSI Average for Production Mode*/
	if (dev->params->production_test == 1) {
		dev->params->rssi_average[rssi_index++] = (char)(rx->rssi);
		if (rssi_index >= MAX_RSSI_SAMPLES)
			rssi_index = 0;
	}

	rx_status.antenna = 0;

	if (rx->rate_flags & ENABLE_VHT_FORMAT) {
		/* Rate */
		if ((rx->rate_or_mcs & MARK_RATE_AS_MCS_INDEX) != 0x80) {
#ifdef DRIVER_DEBUG
			pr_info("Invalid VHT MCS Information\n");
#endif
			rx->rate_or_mcs = 0;/*default to MCS0*/
		} else {
			rx_status.rate_idx = (rx->rate_or_mcs & 0x7f);
		}

		/* NSS */
		if (!rx->nss || rx->nss > 8)
			rx_status.vht_nss = 1;
		else
			 rx_status.vht_nss = rx->nss;

		/* CBW */
		if (rx->rate_flags & ENABLE_CHNL_WIDTH_80MHZ)
			rx_status.flag |= RX_VHT_FLAG_80MHZ;
		else if (rx->rate_flags & ENABLE_CHNL_WIDTH_40MHZ)
			rx_status.flag |= RX_FLAG_40MHZ;

		/* SGI */
		if (rx->rate_flags & ENABLE_SGI)
			rx_status.flag |= RX_FLAG_SHORT_GI;

		rx_status.flag |= RX_FLAG_VHT;
	} else if (rx->rate_flags & ENABLE_11N_FORMAT) {
		/* Rate */
		if ((rx->rate_or_mcs & MARK_RATE_AS_MCS_INDEX) != 0x80) {
#ifdef DRIVER_DEBUG
			pr_info("Invalid HT MCS Information\n");
#endif
			   rx->rate_or_mcs = 0;/*default to MCS0*/
		} else {
			   rx_status.rate_idx = (rx->rate_or_mcs & 0x7f);
		}

		/* CBW */
		if (rx->rate_flags & ENABLE_CHNL_WIDTH_40MHZ)
			rx_status.flag |= RX_FLAG_40MHZ;

		/* SGI */
		if (rx->rate_flags & ENABLE_SGI)
			rx_status.flag |= RX_FLAG_SHORT_GI;

		/* HT Greenfield */
		if (rx->rate_flags & ENABLE_GREEN_FIELD)
			rx_status.flag |= RX_FLAG_HT_GF;

		rx_status.flag |= RX_FLAG_HT;
	} else {
		band = dev->hw->wiphy->bands[rx_status.band];

		if (!WARN_ON_ONCE(!band)) {
			for (i = 0; i < band->n_bitrates; i++) {
				if (rx->rate_or_mcs ==
				    band->bitrates[i].hw_value) {
					rx_status.rate_idx = i;
					break;
				}
			}
		} else {
#ifdef DRIVER_DEBUG
			print_hex_dump(KERN_DEBUG, " ",
				       DUMP_PREFIX_NONE, 16, 1, rx,
				       sizeof(struct wlan_rx_pkt), 1);
#endif
			dev_kfree_skb_any(skb);
			return;
		}
	}

	/* Remove this once hardware supports bip(11w) is available*/
	if (!is_robust_mgmt(skb))
		rx_status.flag |= RX_FLAG_DECRYPTED;

	rx_status.flag |= RX_FLAG_MMIC_STRIPPED;

	if (rx->rx_pkt_status == RX_MIC_FAILURE_TKIP) {
		rx_status.flag |= RX_FLAG_MMIC_ERROR;
	} else if (rx->rx_pkt_status == RX_MIC_FAILURE_CCMP) {
		/*Drop the Frame*/
		dev_kfree_skb_any(skb);
		return;
	}

	if (((hdr->frame_control & IEEE80211_FCTL_FTYPE) ==
	     IEEE80211_FTYPE_MGMT) &&
	    ((hdr->frame_control & IEEE80211_FCTL_STYPE) ==
	     IEEE80211_STYPE_BEACON)) {
		rx_status.mactime = get_unaligned_le64(rx->timestamp);
		rx_status.flag |= RX_FLAG_MACTIME_START;
	}

	/* Beacon Time Stamp */
	if (((hdr->frame_control & IEEE80211_FCTL_FTYPE) ==
	     IEEE80211_FTYPE_MGMT) &&
	    ((hdr->frame_control & IEEE80211_FCTL_STYPE) ==
	     IEEE80211_STYPE_BEACON)) {
		for (i = 0; i < MAX_VIFS; i++) {
			vif = NULL;
			vif = dev->vifs[i];
			if (vif &&
			ether_addr_equal(hdr->addr2, vif->bss_conf.bssid)) {
				unsigned int ts2;
				unsigned int ldelta;

				spin_lock(&tsf_lock);
				dev->params->sync[i].status = 1;
				memcpy(dev->params->sync[i].bssid,
				       vif->bss_conf.bssid, 6);
				memcpy(dev->params->sync[i].ts1,
					&rx->reserved, 8);
				memcpy(&ts2, &rx->reserved[8], 4);
				memcpy(&dev->params->sync[i].ts2,
						&rx->reserved[8], 4);
				memcpy(&ldelta, &rx->reserved[12], 4);
				dev->params->sync[i].atu = 0;
				/* ts2 = get_real_ts2(ts2, ldelta); */
				if (frc_to_atu)
					frc_to_atu(ts2,
						&dev->params->sync[i].atu, 0);
				dev->params->sync[i].atu -= ldelta * 1000;
				spin_unlock(&tsf_lock);
				break;
			}
		}
	}

	memcpy(IEEE80211_SKB_RXCB(skb), &rx_status, sizeof(rx_status));
	ieee80211_rx(dev->hw, skb);
}


void uccp420wlan_ch_prog_complete(int event,
				  struct umac_event_ch_prog_complete *prog_ch,
				  void *context)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)context;

	dev->chan_prog_done = 1;
}

