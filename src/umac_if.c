/*
 * File Name  : umac_if.c
 *
 * This file contains the defintions of helper functions for UMAC comms
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

#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/netdevice.h>

#include "umac_if.h"
#include "core.h"

unsigned char wildcard_ssid[7] = "DIRECT-";

struct cmd_send_recv_cnt cmd_info;

struct lmac_if_data {
	char *name;
	void *context;
};

static struct lmac_if_data __rcu *lmac_if;

static void update_mcs_packet_stat(int mcs_rate_num,
				   int rate_flags,
				   struct mac80211_dev *dev)
{
	if (rate_flags & ENABLE_11N_FORMAT) {
		switch (mcs_rate_num) {
		case 0:
			dev->stats->ht_tx_mcs0_packet_count++;
			break;
		case 1:
			dev->stats->ht_tx_mcs1_packet_count++;
			break;
		case 2:
			dev->stats->ht_tx_mcs2_packet_count++;
			break;
		case 3:
			dev->stats->ht_tx_mcs3_packet_count++;
			break;
		case 4:
			dev->stats->ht_tx_mcs4_packet_count++;
			break;
		case 5:
			dev->stats->ht_tx_mcs5_packet_count++;
			break;
		case 6:
			dev->stats->ht_tx_mcs6_packet_count++;
			break;
		case 7:
			dev->stats->ht_tx_mcs7_packet_count++;
			break;
		case 8:
			dev->stats->ht_tx_mcs8_packet_count++;
			break;
		case 9:
			dev->stats->ht_tx_mcs9_packet_count++;
			break;
		case 10:
			dev->stats->ht_tx_mcs10_packet_count++;
			break;
		case 11:
			dev->stats->ht_tx_mcs11_packet_count++;
			break;
		case 12:
			dev->stats->ht_tx_mcs12_packet_count++;
			break;
		case 13:
			dev->stats->ht_tx_mcs13_packet_count++;
			break;
		case 14:
			dev->stats->ht_tx_mcs14_packet_count++;
			break;
		case 15:
			dev->stats->ht_tx_mcs15_packet_count++;
			break;
		default:
			break;
		}
	} else if (rate_flags & ENABLE_VHT_FORMAT) {
		switch (mcs_rate_num) {
		case 0:
			dev->stats->vht_tx_mcs0_packet_count++;
			break;
		case 1:
			dev->stats->vht_tx_mcs1_packet_count++;
			break;
		case 2:
			dev->stats->vht_tx_mcs2_packet_count++;
			break;
		case 3:
			dev->stats->vht_tx_mcs3_packet_count++;
			break;
		case 4:
			dev->stats->vht_tx_mcs4_packet_count++;
			break;
		case 5:
			dev->stats->vht_tx_mcs5_packet_count++;
			break;
		case 6:
			dev->stats->vht_tx_mcs6_packet_count++;
			break;
		case 7:
			dev->stats->vht_tx_mcs7_packet_count++;
			break;
		case 8:
			dev->stats->vht_tx_mcs8_packet_count++;
			break;
		case 9:
			dev->stats->vht_tx_mcs9_packet_count++;
			break;
		default:
			break;
		}
	}
}


static void get_rate(struct sk_buff *skb,
		     struct cmd_tx_ctrl *txcmd,
		     struct mac80211_dev *dev)
{
	struct ieee80211_rate *rate;
	struct ieee80211_tx_info *c;
	unsigned int index;
	bool is_mcs = false, is_mgd = false;
	struct ieee80211_tx_rate *txrate;
	unsigned char mcs_rate_num = 0;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	int mcs_indx;
	int mgd_rate;
	int prot_type;

	/* Normal Mode*/
	rate = ieee80211_get_tx_rate(dev->hw, IEEE80211_SKB_CB(skb));

	if (rate == NULL) {
		rate = &dev->hw->wiphy->bands[
				dev->hw->conf.chandef.chan->band]->bitrates[0];
		txcmd->num_rates = 1;
		txcmd->rate[0] = rate->hw_value;
		txcmd->rate_retries[0] = 5;
		txcmd->rate_protection_type[0] = USE_PROTECTION_NONE;
		txcmd->rate_preamble_type[0] = DONT_USE_SHORT_PREAMBLE;

		return;
	}

	c = IEEE80211_SKB_CB(skb);
	/* Some defaults*/
	txcmd->num_rates = 0;
	txcmd->stbc_enabled = 0;

	/* BCC (or) LDPC */
	if (c->flags & IEEE80211_TX_CTL_LDPC)
		txcmd->bcc_or_ldpc = 1;
	else
		txcmd->bcc_or_ldpc = 0;

	if (ieee80211_is_data(hdr->frame_control) &&
	    c->flags & IEEE80211_TX_CTL_AMPDU) {
		txcmd->aggregate_mpdu = AMPDU_AGGR_ENABLED;
	}

	for (index = 0; index < 4; index++) {
		txrate = (&c->control.rates[index]);
		txcmd->rate_flags[index] = 0;

		if (txrate->idx < 0)
			continue;

		txcmd->num_spatial_streams[index] = 1;

		/* production test*/
		if (dev->params->production_test == 1 &&
		    dev->params->tx_fixed_mcs_indx != -1) {
			txcmd->rate_preamble_type[index] =
				dev->params->prod_mode_rate_preamble_type;
			txcmd->rate_flags[index] =
				dev->params->prod_mode_rate_flag;
			txcmd->rate[index] = 0x80;
			txcmd->rate[index] |=
			    (dev->params->tx_fixed_mcs_indx);
			txcmd->num_spatial_streams[index] =
			    dev->params->num_spatial_streams;
			txcmd->bcc_or_ldpc =
			    dev->params->prod_mode_bcc_or_ldpc;
			txcmd->stbc_enabled =
			    dev->params->prod_mode_stbc_enabled;
			update_mcs_packet_stat(
			    dev->params->tx_fixed_mcs_indx,
			    txcmd->rate_flags[index], dev);
			txcmd->num_rates++;
			break;
		} else if (dev->params->production_test == 1 &&
			   dev->params->tx_fixed_rate != -1) {
			txcmd->rate_preamble_type[index] =
				dev->params->prod_mode_rate_preamble_type;
			txcmd->rate_flags[index] =
				dev->params->prod_mode_rate_flag;

			txcmd->rate[index] = 0x00;
			if (dev->params->tx_fixed_rate == 55)
				txcmd->rate[index] |=
				 ((dev->params->tx_fixed_rate) /
				  5);
			else
				txcmd->rate[index] |=
				  ((dev->params->tx_fixed_rate *
				    10) / 5);
			txcmd->num_spatial_streams[index] = 1;
			txcmd->bcc_or_ldpc = 0;
			txcmd->stbc_enabled = 0;
			txcmd->num_rates++;
			break;
		}
		/* No input from production_test proc, continue and use
		 * info from mac80211 RC
		 */

		/* It is an VHT MCS rate */
		if (((txrate->flags & IEEE80211_TX_RC_MCS) ||
		     (txrate->flags & IEEE80211_TX_RC_VHT_MCS)) &&
		    txrate->flags & IEEE80211_TX_RC_VHT_MCS) {
			/*idx field is split
			 * into a higher 4 bits (Nss), starts
			 * with 0 and lower 4 bits (MCS number)
			 */
			is_mcs = true;
			mcs_rate_num = (txrate->idx & 0x0F);
			txcmd->num_spatial_streams[index] =
				((txrate->idx & 0xF0) >> 4) + 1;
			/* STBC Enabled/Disabled: valid Nss = 1 */
			if (txcmd->num_spatial_streams[index] == 1 &&
			    (c->flags & IEEE80211_TX_CTL_STBC))
				txcmd->stbc_enabled = 1;

		} else if (((txrate->flags & IEEE80211_TX_RC_MCS) ||
			    (txrate->flags & IEEE80211_TX_RC_VHT_MCS)) &&
			   txrate->flags & IEEE80211_TX_RC_MCS) { /*HT rate */
			is_mcs = true;
			mcs_rate_num  = txrate->idx;

			/* Update No of Spatial streams*/
			if (mcs_rate_num < 8) {
				txcmd->num_spatial_streams[index] = 1;
			} else if (mcs_rate_num > 7  &&
				 mcs_rate_num < 16) {
				txcmd->num_spatial_streams[index] = 2;
			} else  {
				pr_err("UCCP420_WIFI: Invalid MCS index: %d, Supports only 2 spatial streams\n",
			       mcs_rate_num);
			}

			/* Ensures good throughput */
			if (mcs_rate_num > 15 &&
			    dev->params->uccp_num_spatial_streams == 1) {
				mcs_rate_num = 7;
				txcmd->num_spatial_streams[index] = 1;
			} else if (mcs_rate_num > 15 &&
				   dev->params->uccp_num_spatial_streams == 2) {
				mcs_rate_num = 15;
				txcmd->num_spatial_streams[index] = 2;
			}

			/* STBC Enabled/Disabled: valid for Nss=1 */
			if (mcs_rate_num < 8 &&
			    (c->flags & IEEE80211_TX_CTL_STBC))
				txcmd->stbc_enabled = 1;

		} else if (((txrate->flags & IEEE80211_TX_RC_MCS) ||
			    (txrate->flags & IEEE80211_TX_RC_VHT_MCS))) {
			is_mcs = true;
			WARN_ON(1);
		}

		/* Rate FORMAT*/
		if (txrate->flags & IEEE80211_TX_RC_VHT_MCS)
			txcmd->rate_flags[index] |= ENABLE_VHT_FORMAT;
		else if (txrate->flags & IEEE80211_TX_RC_MCS)
			txcmd->rate_flags[index] |= ENABLE_11N_FORMAT;

		mcs_indx = dev->params->mgd_mode_tx_fixed_mcs_indx;
		mgd_rate = dev->params->mgd_mode_tx_fixed_rate;

		/* Rate Index:
		 * From proc (only for data packets)
		 * From RC in mac80211
		 * Can be MCS(HT/VHT) or Rate (11abg)
		 */
		if (ieee80211_is_data(hdr->frame_control) && mcs_indx != -1) {
			is_mgd = true;

			txcmd->rate[index] = 0x80;
			txcmd->rate[index] |= (mcs_indx);
			txcmd->rate_flags[index] =
				dev->params->prod_mode_rate_flag;
			txcmd->num_spatial_streams[index] =
				dev->params->num_spatial_streams;
			txcmd->bcc_or_ldpc =
				dev->params->prod_mode_bcc_or_ldpc;
			txcmd->stbc_enabled =
				dev->params->prod_mode_stbc_enabled;

			update_mcs_packet_stat(mcs_indx,
					       txcmd->rate_flags[index],
					       dev);
		} else if (ieee80211_is_data(hdr->frame_control) &&
			   mgd_rate != -1) {
			is_mgd = true;
			txcmd->rate[index] = 0x80;
			txcmd->rate[index] = 0x00;

			if (mgd_rate == 55)
				txcmd->rate[index] |= ((mgd_rate) / 5);
			else
				txcmd->rate[index] |= ((mgd_rate * 10) / 5);

			txcmd->rate_flags[index] = 0;
			txcmd->num_spatial_streams[index]  = 1;
			txcmd->bcc_or_ldpc         = 0;
			txcmd->stbc_enabled        = 0;
		} else if (is_mcs) { /* idx is MCS */
			/* Now mark MSB to tell LMAC that it is a MCS Index */
			txcmd->rate[index] = 0x80;
			txcmd->rate[index] |= mcs_rate_num;
			update_mcs_packet_stat(mcs_rate_num,
					      txcmd->rate_flags[index],
					      dev);
		} else if (!is_mcs) { /* idx is RATE...*/
			rate = &dev->hw->wiphy->bands[
				c->band]->bitrates[
				c->control.rates[index].idx];
			/* Now mark MSB to tell LMAC that it is a rate*/
			txcmd->rate[index] = 0x00;
			txcmd->rate[index] |= rate->hw_value;
			/* using rate so 11g/11b/11a */
			txcmd->num_spatial_streams[index] = 1;
		}

		if (txcmd->rate_flags[index] & ENABLE_VHT_FORMAT) {
			/*Enabled for all ucast/bcast/mcast frames*/
			txcmd->aggregate_mpdu = AMPDU_AGGR_ENABLED;
		}

		txcmd->rate_retries[index] =
			c->control.rates[index].count;
		if (c->control.rates[index].flags &
		    IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
			txcmd->rate_preamble_type[index] =
				USE_SHORT_PREAMBLE;
		else
			txcmd->rate_preamble_type[index] =
				DONT_USE_SHORT_PREAMBLE;

		prot_type = USE_PROTECTION_NONE;

		if (dev->params->rate_protection_type == 1) {
			/* Protection*/
			if (c->control.rates[index].flags &
			    IEEE80211_TX_RC_USE_CTS_PROTECT)
				prot_type = USE_PROTECTION_CTS2SELF;
			else if (c->control.rates[index].flags &
				 IEEE80211_TX_RC_USE_RTS_CTS)
				prot_type = USE_PROTECTION_RTS;
			else
				prot_type = USE_PROTECTION_NONE;

			if (txcmd->aggregate_mpdu == AMPDU_AGGR_ENABLED)
				prot_type = USE_PROTECTION_RTS;

			if (c->control.rates[index].flags &
			    IEEE80211_TX_RC_40_MHZ_WIDTH)
				prot_type = USE_PROTECTION_RTS;

			/*RTS threshold: Check for PSDU length
			 * Need to add all HW added lenghts to skb,
			 * sw added lengths are already part of skb->len
			 * IV ==> Always SW
			 * MIC for CCMP ==> HW (MMIC for TKIP ==> SW)
			 * ICV ==> HW
			 * FCS ==> HW
			*/
			if (ieee80211_is_data(hdr->frame_control) &&
			    !is_multicast_ether_addr(hdr->addr1) &&
			    ieee80211_has_protected(hdr->frame_control)) {
				if (skb->len +
				    c->control.hw_key->icv_len +
				    FCS_LEN > dev->rts_threshold)
					prot_type = USE_PROTECTION_RTS;
			}

			if (ieee80211_is_data(hdr->frame_control) &&
			    !is_multicast_ether_addr(hdr->addr1) &&
			    !ieee80211_has_protected(hdr->frame_control) &&
			    (skb->len + FCS_LEN > dev->rts_threshold))
				prot_type = USE_PROTECTION_RTS;

		}

		/*No 3rd party device is using this, so diable for now*/
		if (txcmd->rate_flags[index] & ENABLE_VHT_FORMAT)
			prot_type = USE_PROTECTION_NONE;

		txcmd->rate_protection_type[index] = prot_type;


		/* Do not set the flags for Managed Mode, they will come
		 * from proc
		 */
		if (!is_mgd) {
			if (c->control.rates[index].flags &
					IEEE80211_TX_RC_GREEN_FIELD)
				txcmd->rate_flags[index] |=
						ENABLE_GREEN_FIELD;
			if (c->control.rates[index].flags &
					IEEE80211_TX_RC_40_MHZ_WIDTH)
				txcmd->rate_flags[index] |=
						ENABLE_CHNL_WIDTH_40MHZ;
			if (c->control.rates[index].flags &
					IEEE80211_TX_RC_80_MHZ_WIDTH)
				txcmd->rate_flags[index] |=
					ENABLE_CHNL_WIDTH_80MHZ;
			if (c->control.rates[index].flags &
					IEEE80211_TX_RC_SHORT_GI)
				txcmd->rate_flags[index] |= ENABLE_SGI;
		}

		/*Some Sanity Checks*/
		/* Nss-1/2 */
		if (txcmd->num_spatial_streams[index] <= 0 ||
				txcmd->num_spatial_streams[index] > 2)
			txcmd->num_spatial_streams[index] = 1;

		/* VHT 20MHz MCS9 is not valid*/
		if (txrate->flags & IEEE80211_TX_RC_VHT_MCS &&
			((txcmd->rate[index] & 0x7F) == 9) &&
			!(txcmd->rate_flags[index] &
			  ENABLE_CHNL_WIDTH_40MHZ) &&
			!(txcmd->rate_flags[index] &
			  ENABLE_CHNL_WIDTH_80MHZ))
				/* Downgrade to VHT-MCS8-Nss-1 */
				txcmd->rate[index] = 0x88;

		txcmd->num_rates++;
	}
}


static int uccp420wlan_send_cmd(unsigned char *buf,
				unsigned int len,
				unsigned char id)
{
	struct host_mac_msg_hdr *hdr = (struct host_mac_msg_hdr *)buf;
	struct sk_buff *nbuf;
	struct lmac_if_data *p;
	struct mac80211_dev *dev;
	unsigned long irq_flags;

	rcu_read_lock();
	p = (struct lmac_if_data *)(rcu_dereference(lmac_if));

	if (!p) {
		WARN_ON(1);
		rcu_read_unlock();
		return -1;
	}
	dev = p->context;
	nbuf = alloc_skb(len, GFP_ATOMIC);

	if (!nbuf) {
		rcu_read_unlock();
		return -1;
	}
	hdr->id = id;
	hdr->length = len;
	DEBUG_LOG("%s-UMACIF: Sending command:%d, outstanding_cmds: %d\n",
		     p->name, hdr->id, cmd_info.outstanding_ctrl_req);
	hdr->descriptor_id = 0;
	hdr->descriptor_id |= 0x0000ffff;
	memcpy(skb_put(nbuf, len), buf, len);

	dev->stats->outstanding_cmd_cnt = cmd_info.outstanding_ctrl_req;

	/* Take lock to make the control commands sequential in case of SMP*/
	spin_lock_irqsave(&cmd_info.control_path_lock, irq_flags);

	if (cmd_info.outstanding_ctrl_req < MAX_OUTSTANDING_CTRL_REQ) {
		DEBUG_LOG("Sending the CMD, got Access\n");
		hal_ops.send((void *)nbuf, HOST_MOD_ID, UMAC_MOD_ID, 0);
		dev->stats->gen_cmd_send_count++;
	} else {
		DEBUG_LOG("Sending the CMD, Waiting in Queue: %d\n",
			     cmd_info.outstanding_ctrl_req);
		skb_queue_tail(&cmd_info.outstanding_cmd, nbuf);
	}

	/* sent but still no proc_done / unsent due to pending requests */
	cmd_info.outstanding_ctrl_req++;
	spin_unlock_irqrestore(&cmd_info.control_path_lock, irq_flags);
	rcu_read_unlock();

	return 0;
}


int uccp420wlan_prog_reset(unsigned int reset_type, unsigned int lmac_mode)
{
	struct cmd_reset reset;
	struct mac80211_dev *dev;
	struct lmac_if_data *p;
	unsigned int i;

	rcu_read_lock();
	p = (struct lmac_if_data *)(rcu_dereference(lmac_if));

	if (!p) {
		WARN_ON(1);
		rcu_read_unlock();
		return -1;
	}
	rcu_read_unlock();
	dev = p->context;

	memset(&reset, 0, sizeof(struct cmd_reset));

	reset.type = reset_type;

	if (reset_type == LMAC_ENABLE) {
		DEBUG_LOG("ed = %d auto = %d\n", dev->params->ed_sensitivity,
			     dev->params->auto_sensitivity);
		reset.ed_sensitivity = dev->params->ed_sensitivity;
		reset.auto_sensitivity = dev->params->auto_sensitivity;
		reset.include_rxmac_hdr = 0;
		reset.num_spatial_streams =
			dev->params->uccp_num_spatial_streams;
		reset.lmac_mode = lmac_mode;
		reset.antenna_sel = dev->params->antenna_sel;

		if (dev->params->production_test == 0) {
			memcpy(reset.rf_params, dev->params->rf_params_vpd,
			       RF_PARAMS_SIZE);
		} else {
			memcpy(reset.rf_params, dev->params->rf_params,
			       RF_PARAMS_SIZE);
		}

		reset.system_rev = dev->stats->system_rev;
		reset.bg_scan.enabled = dev->params->bg_scan_enable;

		if (reset.bg_scan.enabled) {
			for (i = 0; i < dev->params->bg_scan_num_channels;
			     i++) {
				reset.bg_scan.channel_list[i] =
					dev->params->bg_scan_channel_list[i];
				reset.bg_scan.channel_flags[i] =
					dev->params->bg_scan_channel_flags[i];
			}
			reset.bg_scan.num_channels =
				dev->params->bg_scan_num_channels;
			reset.bg_scan.scan_intval =
				dev->params->bg_scan_intval;
			reset.bg_scan.channel_dur =
				/* Channel spending time */
				dev->params->bg_scan_chan_dur;

			reset.bg_scan.serv_channel_dur =
				/* operating channel spending time */
				dev->params->bg_scan_serv_chan_dur;
		}
	}

	return uccp420wlan_send_cmd((unsigned char *) &reset,
				    sizeof(struct cmd_reset), UMAC_CMD_RESET);
}

int uccp420wlan_proc_tx(void)
{
	struct cmd_tx_ctrl tx_cmd;
	struct sk_buff *nbuf, *nbuf_start, *tmp, *skb;
	unsigned char *data;
	struct lmac_if_data *p;
	struct mac80211_dev *dev;
	struct sk_buff_head *skb_list;
	struct ieee80211_hdr *mac_hdr;
	unsigned int index = 0, descriptor_id = 0, queue = WLAN_AC_BE, pkt = 0;
	u16 hdrlen = 26;

	rcu_read_lock();
	p = (struct lmac_if_data *)(rcu_dereference(lmac_if));

	memset(&tx_cmd, 0, sizeof(struct cmd_tx_ctrl));
	if (!p) {
		WARN_ON(1);
		rcu_read_unlock();
		return -1;
	}
	dev = p->context;
	skb_list = &dev->tx.proc_tx_list[descriptor_id];
	tx_cmd.hdr.id = UMAC_CMD_TX;
	/* Keep the queue num and pool id in descriptor id */
	tx_cmd.hdr.descriptor_id = 0;
	tx_cmd.hdr.descriptor_id |= ((queue & 0x0000FFFF) << 16);
	tx_cmd.hdr.descriptor_id |= (descriptor_id & 0x0000FFFF);
	/* Not used anywhere currently */
	tx_cmd.hdr.length = sizeof(struct cmd_tx_ctrl);

	/* UMAC_CMD_TX*/
	tx_cmd.if_index = 0;
	tx_cmd.queue_num = queue;
	tx_cmd.more_frms = 0;
	tx_cmd.descriptor_id = descriptor_id;
	tx_cmd.num_frames_per_desc = skb_queue_len(skb_list);
	tx_cmd.pkt_gram_payload_len = hdrlen;
	tx_cmd.aggregate_mpdu = AMPDU_AGGR_DISABLED;

	/* production test*/
	tx_cmd.num_rates = 1;
	if (dev->params->tx_fixed_mcs_indx != -1) {
		tx_cmd.rate_preamble_type[index] =
			dev->params->prod_mode_rate_preamble_type;
		tx_cmd.rate_flags[index] =
			dev->params->prod_mode_rate_flag;
		tx_cmd.rate[index] = 0x80;
		tx_cmd.rate[index] |=
		    (dev->params->tx_fixed_mcs_indx);
		tx_cmd.num_spatial_streams[index] =
		    dev->params->num_spatial_streams;
		tx_cmd.bcc_or_ldpc =
		    dev->params->prod_mode_bcc_or_ldpc;
		tx_cmd.stbc_enabled =
		    dev->params->prod_mode_stbc_enabled;
		update_mcs_packet_stat(
		    dev->params->tx_fixed_mcs_indx,
		    tx_cmd.rate_flags[index], dev);
		tx_cmd.num_rates++;
	} else if (dev->params->tx_fixed_rate != -1) {
		tx_cmd.rate_preamble_type[index] =
			dev->params->prod_mode_rate_preamble_type;
		tx_cmd.rate_flags[index] =
			dev->params->prod_mode_rate_flag;

		tx_cmd.rate[index] = 0x00;
		if (dev->params->tx_fixed_rate == 55)
			tx_cmd.rate[index] |=
			 ((dev->params->tx_fixed_rate) /
			  5);
		else
			tx_cmd.rate[index] |=
			  ((dev->params->tx_fixed_rate *
			    10) / 5);
		tx_cmd.num_spatial_streams[index] = 1;
		tx_cmd.bcc_or_ldpc = 0;
		tx_cmd.stbc_enabled = 0;
		tx_cmd.num_rates++;
	}

	nbuf = alloc_skb(sizeof(struct cmd_tx_ctrl) +
			 tx_cmd.num_frames_per_desc *
			 MAX_GRAM_PAYLOAD_LEN, GFP_ATOMIC);

	data = skb_put(nbuf, sizeof(struct cmd_tx_ctrl));
	memset(data, 0, sizeof(struct cmd_tx_ctrl));
	/*store the start for later use*/
	nbuf_start = (struct sk_buff *)data;
	memcpy(data, &tx_cmd,  sizeof(struct cmd_tx_ctrl));
	pkt = 0;
	skb_queue_walk_safe(skb_list, skb, tmp) {
		if (!skb || (pkt > tx_cmd.num_frames_per_desc))
			break;

		mac_hdr = (struct ieee80211_hdr *)skb->data;
		/* Complete packet length*/
		((struct cmd_tx_ctrl *)nbuf_start)->pkt_length[pkt] = skb->len;
		skb_put(nbuf, MAX_GRAM_PAYLOAD_LEN);
		memcpy((unsigned char *)nbuf_start +
			sizeof(struct cmd_tx_ctrl)+
			(pkt * MAX_GRAM_PAYLOAD_LEN),
			mac_hdr, hdrlen);

		skb_pull(skb, hdrlen);
		if (hal_ops.map_tx_buf(descriptor_id, pkt,
				       skb->data, skb->len)) {
			rcu_read_unlock();
			dev_kfree_skb_any(nbuf);
			return -30;
		}
		pkt++;
	}
	hal_ops.send((void *)nbuf, HOST_MOD_ID, UMAC_MOD_ID,
			(void *) skb_list);
	/* increment tx_cmd_send_count to keep track of number of
	 * tx_cmd send
	 */
	if (skb_queue_len(skb_list) == 1)
		dev->stats->tx_cmd_send_count_single++;
	else if (skb_queue_len(skb_list) > 1)
		dev->stats->tx_cmd_send_count_multi++;

	rcu_read_unlock();

	return 0;
}

int uccp420wlan_prog_txpower(unsigned int txpower)
{
	struct cmd_tx_pwr power;

	memset(&power, 0, sizeof(struct cmd_tx_pwr));
	power.tx_pwr = txpower;
	power.if_index = 0;

	return uccp420wlan_send_cmd((unsigned char *) &power,
				    sizeof(struct cmd_tx_pwr),
				    UMAC_CMD_TX_POWER);
}


int uccp420wlan_prog_btinfo(unsigned int bt_state)
{
	struct cmd_bt_info bt_info;

	memset(&bt_info, 0, sizeof(struct cmd_bt_info));
	bt_info.bt_state = bt_state;

	return uccp420wlan_send_cmd((unsigned char *) &bt_info,
					sizeof(struct cmd_bt_info),
					UMAC_CMD_BT_INFO);
}


int uccp420wlan_prog_vif_ctrl(int index,
		unsigned char *mac_addr,
		unsigned int vif_type,
		unsigned int op)
{
	struct cmd_vifctrl vif_ctrl;

	memset(&vif_ctrl, 0, sizeof(struct cmd_vifctrl));
	vif_ctrl.mode = vif_type;
	memcpy(vif_ctrl.mac_addr, mac_addr, 6);
	vif_ctrl.if_index = index;
	vif_ctrl.if_ctrl = op;

	return uccp420wlan_send_cmd((unsigned char *) &vif_ctrl,
				    sizeof(struct cmd_vifctrl),
				    UMAC_CMD_VIF_CTRL);
}


int uccp420wlan_prog_mcast_addr_cfg(unsigned char *mcast_addr,
				    unsigned int op)
{
	struct cmd_mcst_addr_cfg mcast_config;

	memset(&mcast_config, 0, sizeof(struct cmd_mcst_addr_cfg));

	mcast_config.op = op;
	memcpy(mcast_config.mac_addr, mcast_addr, 6);

	return uccp420wlan_send_cmd((unsigned char *) &mcast_config,
				    sizeof(struct cmd_mcst_addr_cfg),
				    UMAC_CMD_MCST_ADDR_CFG);
}


int uccp420wlan_prog_mcast_filter_control(unsigned int mcast_filter_enable)
{
	struct cmd_mcst_filter_ctrl mcast_ctrl;

	memset(&mcast_ctrl, 0, sizeof(struct cmd_mcst_filter_ctrl));
	mcast_ctrl.ctrl = mcast_filter_enable;

	return uccp420wlan_send_cmd((unsigned char *) &mcast_ctrl,
				    sizeof(struct cmd_mcst_filter_ctrl),
				    UMAC_CMD_MCST_FLTR_CTRL);
}


int uccp420wlan_prog_vht_bform(unsigned int vht_beamform_status,
				  unsigned int vht_beamform_period)
{
	struct cmd_vht_beamform vht_beamform;

	memset(&vht_beamform, 0, sizeof(struct cmd_vht_beamform));

	vht_beamform.vht_beamform_status = vht_beamform_status;
	vht_beamform.vht_beamform_period = vht_beamform_period;

	return uccp420wlan_send_cmd((unsigned char *) &vht_beamform,
				    sizeof(struct cmd_vht_beamform),
				    UMAC_CMD_VHT_BEAMFORM_CTRL);
}


int uccp420wlan_prog_roc(unsigned int roc_status,
			 unsigned int roc_channel,
			 unsigned int roc_duration)
{
	struct cmd_roc cmd_roc;

	memset(&cmd_roc, 0, sizeof(struct cmd_roc));

	cmd_roc.roc_status	= roc_status;
	cmd_roc.roc_channel	= roc_channel;
	cmd_roc.roc_duration	= roc_duration;

	return uccp420wlan_send_cmd((unsigned char *) &cmd_roc,
			sizeof(struct cmd_roc), UMAC_CMD_ROC_CTRL);
}


int uccp420wlan_prog_nw_selection(unsigned int nw_select_enabled,
				  unsigned char *mac_addr)
{
	struct cmd_nw_selection nw_select;
	unsigned char req_ie[] = {0xdd, 0x7d, 0x00, 0x50, 0xf2, 0x04, 0x10,
				  0x4a, 0x00, 0x01, 0x10, 0x10, 0x3a, 0x00,
				  0x01, 0x01, 0x10, 0x08, 0x00, 0x02, 0x23,
				  0x88, 0x10, 0x47, 0x00, 0x10, 0x09, 0x0d,
				  0xf9, 0x4b, 0xf7, 0xab, 0x54, 0x75, 0x8b,
				  0x4b, 0x91, 0x94, 0x5a, 0x3c, 0xb0, 0xda,
				  0x10, 0x54, 0x00, 0x08, 0x00, 0x01, 0x00,
				  0x50, 0xf2, 0x04, 0x00, 0x01, 0x10, 0x3c,
				  0x00, 0x01, 0x01, 0x10, 0x02, 0x00, 0x02,
				  0x00, 0x00, 0x10, 0x09, 0x00, 0x02, 0x00,
				  0x00, 0x10, 0x12, 0x00, 0x02, 0x00, 0x00,
				  0x10, 0x21, 0x00, 0x09, 0x48, 0x65, 0x6c,
				  0x6c, 0x6f, 0x73, 0x6f, 0x66, 0x74, 0x10,
				  0x23, 0x00, 0x06, 0x57, 0x50, 0x53, 0x32,
				  0x2e, 0x30, 0x10, 0x24, 0x00, 0x04, 0x30,
				  0x2e, 0x38, 0x78, 0x10, 0x11, 0x00, 0x02,
				  0x4d, 0x4d, 0x10, 0x49, 0x00, 0x09, 0x00,
				  0x37, 0x2a, 0x00, 0x01, 0x20, 0x03, 0x01,
				  0x01, 0xdd, 0x11, 0x50, 0x6f, 0x9a, 0x09,
				  0x02, 0x02, 0x00, 0x23, 0x00, 0x06, 0x05,
				  0x00, 0x58, 0x58, 0x04, 0x51, 0x0b};

	unsigned char resp_ie[] = {0xdd, 0x70, 0x00, 0x50, 0xf2, 0x04, 0x10,
				  0x4a, 0x00, 0x01, 0x10, 0x10, 0x44, 0x00,
				  0x01, 0x01, 0x10, 0x3b, 0x00, 0x01, 0x00,
				  0x10, 0x47, 0x00, 0x10, 0x09, 0x0d, 0xf9,
				  0x4b, 0xf7, 0xab, 0x54, 0x75, 0x8b, 0x4b,
				  0x91, 0x94, 0x5a, 0x3c, 0xb0, 0xda, 0x10,
				  0x21, 0x00, 0x09, 0x48, 0x65, 0x6c, 0x6c,
				  0x6f, 0x73, 0x6f, 0x66, 0x74, 0x10, 0x23,
				  0x00, 0x06, 0x57, 0x50, 0x53, 0x32, 0x2e,
				  0x30, 0x10, 0x24, 0x00, 0x04, 0x30, 0x2e,
				  0x38, 0x78, 0x10, 0x42, 0x00, 0x04, 0x30,
				  0x30, 0x31, 0x34, 0x10, 0x54, 0x00, 0x08,
				  0x00, 0x01, 0x00, 0x50, 0xf2, 0x04, 0x00,
				  0x01, 0x10, 0x11, 0x00, 0x02, 0x4d, 0x4d,
				  0x10, 0x08, 0x00, 0x02, 0x23, 0x88, 0x10,
				  0x49, 0x00, 0x06, 0x00, 0x37, 0x2a, 0x00,
				  0x01, 0x20, 0xdd, 0x23, 0x50, 0x6f, 0x9a,
				  0x09, 0x02, 0x02, 0x00, 0x23, 0x00, 0x0d,
				  0x17, 0x00, mac_addr[0], mac_addr[1],
				  mac_addr[2], mac_addr[3], mac_addr[4],
				  mac_addr[5], 0x01, 0x88, 0x00, 0x01, 0x00,
				  0x50, 0xf2, 0x04, 0x00, 0x01, 0x00, 0x10,
				  0x11, 0x00, 0x02, 0x4d, 0x4d};

	memset(&nw_select, 0, sizeof(struct cmd_nw_selection));
	nw_select.p2p_selection = nw_select_enabled;
	memcpy(nw_select.ssid.ssid, wildcard_ssid, 7);
	nw_select.ssid.len = 7;
	nw_select.scan_req_ie_len = sizeof(req_ie);
	nw_select.scan_resp_ie_len = sizeof(resp_ie);

	pr_err("req_len = %d, resp_len = %d\n",
	       nw_select.scan_req_ie_len, nw_select.scan_resp_ie_len);

	memcpy(nw_select.scan_req_ie, req_ie, nw_select.scan_req_ie_len);
	memcpy(nw_select.scan_resp_ie, resp_ie, nw_select.scan_resp_ie_len);

	return uccp420wlan_send_cmd((unsigned char *) &nw_select,
				    sizeof(struct cmd_nw_selection),
				    UMAC_CMD_NW_SELECTION);

}


int uccp420wlan_prog_peer_key(int vif_index,
			      unsigned char *vif_addr,
			      unsigned int op,
			      unsigned int key_id,
			      unsigned int key_type,
			      unsigned int cipher_type,
			      struct umac_key *key)
{
	struct cmd_setkey peer_key;

	memset(&peer_key, 0, sizeof(struct cmd_setkey));

	peer_key.if_index = vif_index;
	/* memcpy(peer_key.vif_addr, vif_addr, ETH_ALEN); */
	peer_key.ctrl = op;
	peer_key.key_id = key_id;
	ether_addr_copy(peer_key.mac_addr, key->peer_mac);

	peer_key.key_type = key_type;
	peer_key.cipher_type = cipher_type;
	memcpy(peer_key.key, key->key, MAX_KEY_LEN);
	peer_key.key_len = MAX_KEY_LEN;

	if (key->tx_mic) {
		memcpy(peer_key.key + MAX_KEY_LEN, key->tx_mic, TKIP_MIC_LEN);
		peer_key.key_len += TKIP_MIC_LEN;
	}
	if (key->rx_mic) {
		memcpy(peer_key.key + MAX_KEY_LEN + TKIP_MIC_LEN, key->rx_mic,
		       TKIP_MIC_LEN);
		peer_key.key_len += TKIP_MIC_LEN;
	}
	peer_key.rsc_len = 6;
	memset(peer_key.rsc, 0, 6);

	return uccp420wlan_send_cmd((unsigned char *) &peer_key,
				    sizeof(struct cmd_setkey), UMAC_CMD_SETKEY);
}


int uccp420wlan_prog_if_key(int vif_index,
			    unsigned char *vif_addr,
			    unsigned int op,
			    unsigned int key_id,
			    unsigned int cipher_type,
			    struct umac_key *key)
	{
	struct cmd_setkey if_key;

	memset(&if_key, 0, sizeof(struct cmd_setkey));

	if_key.if_index = vif_index;
	/* memcpy(if_key.vif_addr, vif_addr, 6); */
	if_key.key_id = key_id;
	if_key.ctrl = op;

	if (op == KEY_CTRL_ADD) {
		if_key.cipher_type = cipher_type;

		if (cipher_type == CIPHER_TYPE_TKIP ||	cipher_type ==
		    CIPHER_TYPE_CCMP) {
			memcpy(if_key.key, key->key, MAX_KEY_LEN);
			if_key.key_len = MAX_KEY_LEN;

			if (key->tx_mic) {
				memcpy(if_key.key + MAX_KEY_LEN, key->tx_mic,
				       TKIP_MIC_LEN);
				if_key.key_len += TKIP_MIC_LEN;
			}
		} else {
			if_key.key_len =
				(cipher_type == CIPHER_TYPE_WEP40) ? 5 : 13;
			memcpy(if_key.key, key->key, if_key.key_len);
		}
	}

	if_key.rsc_len = 6;
	if_key.key_type = KEY_TYPE_BCAST;
	memset(if_key.rsc, 0, 6);
	memset(if_key.mac_addr, 0xff, 6);

	return uccp420wlan_send_cmd((unsigned char *) &if_key,
				    sizeof(struct cmd_setkey), UMAC_CMD_SETKEY);
}

int uccp420wlan_prog_ba_session_data(unsigned int op,
				     unsigned short tid,
				     unsigned short *ssn,
				     unsigned short ba_policy,
				     unsigned char *vif_addr,
				     unsigned char *peer_addr)
{
	struct cmd_ht_ba ba_cmd;
	int index;
	struct mac80211_dev *dev;
	struct lmac_if_data *p;

	rcu_read_lock();
	p = (struct lmac_if_data *)(rcu_dereference(lmac_if));

	if (!p) {
		WARN_ON(1);
		rcu_read_unlock();
		return -1;
	}

	rcu_read_unlock();
	dev = p->context;

	memset(&ba_cmd, 0, sizeof(struct cmd_ht_ba));

	for (index = 0; index < dev->params->num_vifs; index++) {
		if (dev->if_mac_addresses[index].addr[5] == vif_addr[5])
			break;
	}

	if (index == dev->params->num_vifs) {
		DEBUG_LOG("no VIF found\n");
		return -1;
	}

	ba_cmd.if_index = index;
	ba_cmd.op = op;
	ba_cmd.policy = ba_policy;
	ba_cmd.tid = tid;
	ba_cmd.ssn = *ssn;
	ether_addr_copy(ba_cmd.vif_addr, vif_addr);
	ether_addr_copy(ba_cmd.peer_addr, peer_addr);

	return uccp420wlan_send_cmd((unsigned char *) &ba_cmd,
				    sizeof(struct cmd_ht_ba),
				    UMAC_CMD_BA_SESSION_INFO);
}


int uccp420wlan_scan(int index,
		     struct scan_req *req)
{
	struct cmd_scan *scan;
	unsigned char i;
	struct mac80211_dev *dev;
	struct lmac_if_data *p;

	rcu_read_lock();
	p = (struct lmac_if_data *)(rcu_dereference(lmac_if));

	if (!p) {
		WARN_ON(1);
		rcu_read_unlock();
		return -1;
	}

	rcu_read_unlock();
	dev = p->context;

	scan = kmalloc(sizeof(struct cmd_scan) +
		       req->ie_len, GFP_KERNEL);

	if (scan == NULL) {
		DEBUG_LOG("%s: Failed to allocate memory\n", __func__);
		return -ENOMEM;
	}

	memset(scan, 0, sizeof(struct cmd_scan));

	scan->if_index = index;

	/* We support 4 SSIDs */
	scan->n_ssids = req->n_ssids;
	scan->n_channel = req->n_channels;
	scan->type = dev->params->scan_type;

	for (i = 0; i < scan->n_channel; i++) {
		scan->channel_list[i] =
			(ieee80211_frequency_to_channel(req->center_freq[i]));
		scan->chan_max_power[i] = req->freq_max_power[i];

		/* scan->chan_max_antenna_gain[i] =
		 * req->freq_max_antenna_gain[i];
		 */

		/* In mac80211 the flags are u32 but for scanning we need
		 * only first PASSIVE_SCAN flag, remaining flags may be used
		 * in future.
		 */
		if ((req->chan_flags[i] & IEEE80211_CHAN_NO_IR) ||
		    (req->chan_flags[i] & IEEE80211_CHAN_RADAR)) {
			scan->chan_flags[i] = PASSIVE;
		} else {
			scan->chan_flags[i] = ACTIVE;
		}
	}

	scan->p2p_probe = req->p2p_probe;

	scan->extra_ies_len = req->ie_len;

	if (req->ie_len)
		memcpy(scan->extra_ies, req->ie, req->ie_len);

	if (req->n_ssids > 0) {
		for (i = 0; i < scan->n_ssids; i++) {
			scan->ssids[i].len = req->ssids[i].ssid_len;
			if (scan->ssids[i].len > 0)
				memcpy(scan->ssids[i].ssid, req->ssids[i].ssid,
				       req->ssids[i].ssid_len);
		}
	}
	DEBUG_LOG("Scan request ie\n");
	DEBUG_LOG("	len = %d n_channel = %d, n_ssids = %d\n",
			req->ie_len,
			scan->n_channel,
			scan->n_ssids);
	DEBUG_LOG("	if_index = %d type = %d p2p = %d\n",
			scan->if_index,
			scan->type,
			scan->p2p_probe);

	for (i = 0; i < scan->n_ssids; i++) {
		if (scan->ssids[i].len != 0)
			DEBUG_LOG("SSID: %s\n", scan->ssids[i].ssid);
		else
			DEBUG_LOG("SSID: EMPTY\n");
	}

	DEBUG_LOG("CHANNEL_LIST: Channel ==> Channel Flags\n");

	for (i = 0; i < scan->n_channel; i++)
		DEBUG_LOG("Index %d: %d ==> %d\n", i,
				scan->channel_list[i], scan->chan_flags[i]);

	dev->stats->umac_scan_req++;

	uccp420wlan_send_cmd((unsigned char *)scan, sizeof(struct cmd_scan) +
			     req->ie_len, UMAC_CMD_SCAN);
	kfree(scan);

	return 0;
}


int uccp420wlan_scan_abort(int index)
{
	struct cmd_scan_abort *scan_abort = NULL;

	scan_abort = (struct cmd_scan_abort *)
		kmalloc(sizeof(struct cmd_scan_abort), GFP_KERNEL);

	if (scan_abort == NULL) {
		DEBUG_LOG("%s: Failed to allocate memory\n", __func__);
		return -ENOMEM;
	}

	memset(scan_abort, 0, sizeof(struct cmd_scan_abort));

	scan_abort->if_index = index;

	uccp420wlan_send_cmd((unsigned char *)scan_abort,
			     sizeof(struct cmd_scan_abort),
			     UMAC_CMD_SCAN_ABORT);

	kfree(scan_abort);
	scan_abort = NULL;

	return 0;
}


int uccp420wlan_prog_channel(unsigned int prim_ch,
			     unsigned int ch_no1,
			     unsigned int ch_no2,
			     unsigned int ch_width,
#ifdef MULTI_CHAN_SUPPORT
			     unsigned int vif_index,
#endif
			     unsigned int freq_band)
{
	struct cmd_channel channel;

	memset(&channel, 0, sizeof(struct cmd_channel));
	channel.primary_ch_number = prim_ch;
	channel.channel_number1 = ch_no1;
	channel.channel_number2 = ch_no2;

	switch (ch_width) {
	case 0:
	case 1:
		channel.channel_bw = 0;
		break;
	case 2:
		channel.channel_bw = 1;
		break;
	case 3:
		channel.channel_bw = 2;
		break;
	case 4:
	case 5:
		channel.channel_bw = 3;
		break;
	default:
		break;
	}

	channel.freq_band = freq_band;
#ifdef MULTI_CHAN_SUPPORT
	channel.vif_index = vif_index;
#endif

	return uccp420wlan_send_cmd((unsigned char *) &channel,
				    sizeof(struct cmd_channel),
				    UMAC_CMD_CHANNEL);
}


#ifdef MULTI_CHAN_SUPPORT
int uccp420wlan_prog_chanctx_time_info(void)
{
	struct cmd_chanctx_time_config time_cfg;
	int i = 0;
	int j = 0;
	struct mac80211_dev *dev = NULL;
	struct lmac_if_data *p = NULL;
	struct ieee80211_chanctx_conf *curr_conf = NULL;
	struct umac_chanctx *curr_ctx = NULL;
	int freq = 0;

	rcu_read_lock();

	p = (struct lmac_if_data *)(rcu_dereference(lmac_if));

	if (!p) {
		WARN_ON(1);
		rcu_read_unlock();
		return -1;
	}

	rcu_read_unlock();

	dev = p->context;

	memset(&time_cfg, 0, sizeof(struct cmd_chanctx_time_config));

	rcu_read_lock();

	for (i = 0; i < MAX_CHANCTX; i++) {
		curr_conf = rcu_dereference(dev->chanctx[i]);

		if (curr_conf) {
			curr_ctx = (struct umac_chanctx *)curr_conf->drv_priv;

			if (curr_ctx->nvifs) {
				freq = curr_conf->def.chan->center_freq;

				time_cfg.info[j].chan =
					ieee80211_frequency_to_channel(freq);
				time_cfg.info[j].percentage =
					(100 / dev->num_active_chanctx);
				j++;
			}
		}
	}

	rcu_read_unlock();

	return uccp420wlan_send_cmd((unsigned char *)&time_cfg,
				    sizeof(struct cmd_chanctx_time_config),
				    UMAC_CMD_CHANCTX_TIME_INFO);
}
#endif


int uccp420wlan_prog_ps_state(int index,
			      unsigned char *vif_addr,
			      unsigned int powersave_state)
{
	struct cmd_ps ps_cfg;

	memset(&ps_cfg, 0, sizeof(struct cmd_ps));
	ps_cfg.mode = powersave_state;
	ps_cfg.if_index = index;

	return uccp420wlan_send_cmd((unsigned char *)&ps_cfg,
				    sizeof(struct cmd_ps), UMAC_CMD_PS);
}


int uccp420wlan_prog_tx(unsigned int queue,
			unsigned int more_frms,
			unsigned int descriptor_id)
{
	struct cmd_tx_ctrl tx_cmd;
	struct sk_buff *nbuf, *nbuf_start;
	unsigned char *data;
	struct lmac_if_data *p;
	struct mac80211_dev *dev;
	struct umac_vif *uvif;
	struct sk_buff *skb, *skb_first, *tmp;
	struct sk_buff_head *txq = NULL;
	struct ieee80211_hdr *mac_hdr;
	struct ieee80211_tx_info *tx_info_first;
	unsigned int hdrlen, pkt = 0;
	int vif_index;
	__u16 fc;
	unsigned long irq_flags, tx_irq_flags;
#ifdef MULTI_CHAN_SUPPORT
	int chan_id = 0;
#endif

	memset(&tx_cmd, 0, sizeof(struct cmd_tx_ctrl));

	rcu_read_lock();
	p = (struct lmac_if_data *)(rcu_dereference(lmac_if));

	if (!p) {
		WARN_ON(1);
		rcu_read_unlock();
		return -1;
	}

	dev = p->context;
	spin_lock_irqsave(&dev->tx.lock, tx_irq_flags);
#ifdef MULTI_CHAN_SUPPORT
	txq = &dev->tx.pkt_info[dev->curr_chanctx_idx][descriptor_id].pkt;
#else
	txq = &dev->tx.pkt_info[descriptor_id].pkt;
#endif
	skb_first = skb_peek(txq);

	if (!skb_first) {
		spin_unlock_irqrestore(&dev->tx.lock, tx_irq_flags);
		rcu_read_unlock();
		return -10;
	}

	tx_info_first = IEEE80211_SKB_CB(skb_first);

	mac_hdr = (struct ieee80211_hdr *)skb_first->data;
	fc = mac_hdr->frame_control;
	hdrlen = ieee80211_hdrlen(fc);
	vif_index = vif_addr_to_index(mac_hdr->addr2, dev);

	/* GET The security Header Length only for data/qos-data/unicast PMF
	 * for 11W case.
	 */
	if ((ieee80211_is_data(fc) ||
	     ieee80211_is_data_qos(fc))
	    && ieee80211_has_protected(fc)) {
		DEBUG_LOG("%s:cipher: %d,icv_len: %d,iv_len: %d,keylen:%d\n",
			     __func__,
			     tx_info_first->control.hw_key->cipher,
			     tx_info_first->control.hw_key->icv_len,
			     tx_info_first->control.hw_key->iv_len,
			     tx_info_first->control.hw_key->keylen);

		/* iv_len is always the header ahd
		 * icv_len is always the trailer
		 * include only iv_len
		 */
		hdrlen += tx_info_first->control.hw_key->iv_len;
	}

	/* For injected frames (wlantest) hw_key is not set,as PMF uses
	 * CCMP always so hardcode this to CCMP IV LEN 8.
	 * For Auth3: It is completely handled in SW (mac80211).
	 */
	if (ieee80211_is_unicast_robust_mgmt_frame(skb_first) &&
	    ieee80211_has_protected(fc)) {
		hdrlen += 8;
		tx_cmd.force_encrypt = 1;
	}

	/* separate in to up to TSF and From TSF*/
	if (ieee80211_is_beacon(fc) || ieee80211_is_probe_resp(fc))
		hdrlen += 8; /* Timestamp*/

	/* HAL UMAC-LMAC HDR*/
	tx_cmd.hdr.id = UMAC_CMD_TX;
	/* Keep the queue num and pool id in descriptor id */
	tx_cmd.hdr.descriptor_id = 0;
	tx_cmd.hdr.descriptor_id |= ((queue & 0x0000FFFF) << 16);
	tx_cmd.hdr.descriptor_id |= (descriptor_id & 0x0000FFFF);
	/* Not used anywhere currently */
	tx_cmd.hdr.length = sizeof(struct cmd_tx_ctrl);

	/* UMAC_CMD_TX*/
	tx_cmd.if_index = vif_index;
	tx_cmd.queue_num = queue;
	tx_cmd.more_frms = more_frms;
	tx_cmd.descriptor_id = descriptor_id;
	tx_cmd.num_frames_per_desc = skb_queue_len(txq);
	tx_cmd.pkt_gram_payload_len = hdrlen;
	tx_cmd.aggregate_mpdu = AMPDU_AGGR_DISABLED;

	uvif = (struct umac_vif *) (tx_info_first->control.vif->drv_priv);

	nbuf = alloc_skb(sizeof(struct cmd_tx_ctrl) +
			 tx_cmd.num_frames_per_desc *
			 MAX_GRAM_PAYLOAD_LEN, GFP_ATOMIC);

	if (!nbuf) {
		spin_unlock_irqrestore(&dev->tx.lock, tx_irq_flags);
		rcu_read_unlock();
		return -20;
	}

	 /* Get the rate for first packet as all packets have same rate */
	get_rate(skb_first, &tx_cmd, dev);

	data = skb_put(nbuf, sizeof(struct cmd_tx_ctrl));
	memset(data, 0, sizeof(struct cmd_tx_ctrl));
	/*store the start for later use*/
	nbuf_start = (struct sk_buff *)data;
	memcpy(data, &tx_cmd,  sizeof(struct cmd_tx_ctrl));

	DEBUG_LOG("%s-UMACTX: TX Frame, Queue = %d, descriptord_id = %d\n",
		     dev->name,
		     tx_cmd.queue_num, tx_cmd.descriptor_id);
	DEBUG_LOG("		num_frames= %d qlen: %d len = %d\n",
		     tx_cmd.num_frames_per_desc, skb_queue_len(txq),
		     nbuf->len);

	DEBUG_LOG("%s-UMACTX: Num rates = %d, %x, %x, %x, %x\n",
		     dev->name,
		     tx_cmd.num_rates,
		     tx_cmd.rate[0],
		     tx_cmd.rate[1],
		     tx_cmd.rate[2],
		     tx_cmd.rate[3]);

	skb_queue_walk_safe(txq, skb, tmp) {
		if (!skb || (pkt > tx_cmd.num_frames_per_desc))
			break;

		mac_hdr = (struct ieee80211_hdr *)skb->data;

		/* Only for Non-Qos and MGMT frames, for Qos-Data
		 * mac80211 handles the sequence no generation
		 */
		if (tx_info_first->flags &
		    IEEE80211_TX_CTL_ASSIGN_SEQ) {
			if (tx_info_first->flags &
			    IEEE80211_TX_CTL_FIRST_FRAGMENT) {
				uvif->seq_no += 0x10;
			}

			mac_hdr->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
			mac_hdr->seq_ctrl |= cpu_to_le16(uvif->seq_no);
		}

		/* Need it for tx_status later */
#ifdef MULTI_CHAN_SUPPORT
		dev->tx.pkt_info[dev->curr_chanctx_idx][descriptor_id].hdr_len =
			hdrlen;
		dev->tx.pkt_info[dev->curr_chanctx_idx][descriptor_id].queue =
			queue;
#else
		dev->tx.pkt_info[descriptor_id].hdr_len = hdrlen;
		dev->tx.pkt_info[descriptor_id].queue = queue;
#endif

		/* Complete packet length */
		((struct cmd_tx_ctrl *)nbuf_start)->pkt_length[pkt] = skb->len;

		/* We move the 11hdr from skb to UMAC_CMD_TX, this is part of
		 * online DMA changes, HW expects only data portion
		 * While DMA. Not requried for loopback
		 */
		skb_put(nbuf, MAX_GRAM_PAYLOAD_LEN);

		memcpy((unsigned char *)nbuf_start +
			sizeof(struct cmd_tx_ctrl)+
			(pkt * MAX_GRAM_PAYLOAD_LEN),
			mac_hdr, hdrlen);

		skb_pull(skb, hdrlen);
		if (hal_ops.map_tx_buf(descriptor_id, pkt,
				       skb->data, skb->len)) {
			spin_unlock_irqrestore(&dev->tx.lock, tx_irq_flags);
			rcu_read_unlock();
			dev_kfree_skb_any(nbuf);
			return -30;
		}

		pkt++;
	}

#ifdef PERF_PROFILING
	if (dev->params->driver_tput == 0) {
#endif

		/* SDK: Check if we can use the same txq initialized before in
		 * the function here */
#ifdef MULTI_CHAN_SUPPORT
		chan_id = dev->curr_chanctx_idx;
		txq = &dev->tx.pkt_info[chan_id][descriptor_id].pkt;
#else
		txq = &dev->tx.pkt_info[descriptor_id].pkt;
#endif

		spin_lock_irqsave(&cmd_info.control_path_lock, irq_flags);

		hal_ops.send((void *)nbuf,
			     HOST_MOD_ID,
			     UMAC_MOD_ID,
			     (void *)txq);

		spin_unlock_irqrestore(&cmd_info.control_path_lock, irq_flags);

		/* increment tx_cmd_send_count to keep track of number of
		 * tx_cmd send
		 */
		if (skb_queue_len(txq) == 1)
			dev->stats->tx_cmd_send_count_single++;
		else if (skb_queue_len(txq) > 1)
			dev->stats->tx_cmd_send_count_multi++;
#ifdef PERF_PROFILING
	}
#endif

	spin_unlock_irqrestore(&dev->tx.lock, tx_irq_flags);
	rcu_read_unlock();

	return 0;
}


int uccp420wlan_prog_vif_short_slot(int index,
				    unsigned char *vif_addr,
				    unsigned int use_short_slot)
{
	struct cmd_vif_cfg vif_cfg;

	memset(&vif_cfg, 0, sizeof(struct cmd_vif_cfg));
	vif_cfg.changed_bitmap = SHORTSLOT_CHANGED;
	vif_cfg.use_short_slot = use_short_slot;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp420wlan_send_cmd((unsigned char *)&vif_cfg,
				    sizeof(struct cmd_vif_cfg),
				    UMAC_CMD_VIF_CFG);
}


int uccp420wlan_prog_vif_atim_window(int index,
				     unsigned char *vif_addr,
				     unsigned int atim_window)
{
	struct cmd_vif_cfg vif_cfg;

	memset(&vif_cfg, 0, sizeof(struct cmd_vif_cfg));
	vif_cfg.changed_bitmap = ATIMWINDOW_CHANGED;
	vif_cfg.atim_window = atim_window;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp420wlan_send_cmd((unsigned char *)&vif_cfg,
				    sizeof(struct cmd_vif_cfg),
				    UMAC_CMD_VIF_CFG);
}


int uccp420wlan_prog_long_retry(int index,
				unsigned char *vif_addr,
				unsigned int long_retry)
{
	struct cmd_vif_cfg vif_cfg;

	memset(&vif_cfg, 0, sizeof(struct cmd_vif_cfg));
	vif_cfg.changed_bitmap = LONGRETRY_CHANGED;
	vif_cfg.long_retry = long_retry;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp420wlan_send_cmd((unsigned char *)&vif_cfg,
				    sizeof(struct cmd_vif_cfg),
				    UMAC_CMD_VIF_CFG);

}


int uccp420wlan_prog_short_retry(int index,
				 unsigned char *vif_addr,
				 unsigned int short_retry)
{

	struct cmd_vif_cfg vif_cfg;

	memset(&vif_cfg, 0, sizeof(struct cmd_vif_cfg));
	vif_cfg.changed_bitmap = SHORTRETRY_CHANGED;
	vif_cfg.short_retry = short_retry;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp420wlan_send_cmd((unsigned char *)&vif_cfg,
				    sizeof(struct cmd_vif_cfg),
				    UMAC_CMD_VIF_CFG);


}


int uccp420wlan_prog_vif_basic_rates(int index,
				     unsigned char *vif_addr,
				     unsigned int basic_rate_set)
{
	struct cmd_vif_cfg vif_cfg;

	memset(&vif_cfg, 0, sizeof(struct cmd_vif_cfg));
	vif_cfg.changed_bitmap = BASICRATES_CHANGED;
	vif_cfg.basic_rate_set = basic_rate_set;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp420wlan_send_cmd((unsigned char *)&vif_cfg,
				    sizeof(struct cmd_vif_cfg),
				    UMAC_CMD_VIF_CFG);


}


int uccp420wlan_prog_vif_aid(int index,
			     unsigned char *vif_addr,
			     unsigned int aid)
{
	struct cmd_vif_cfg vif_cfg;

	memset(&vif_cfg, 0, sizeof(struct cmd_vif_cfg));
	vif_cfg.changed_bitmap = AID_CHANGED;
	vif_cfg.aid = aid;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp420wlan_send_cmd((unsigned char *)&vif_cfg,
				    sizeof(struct cmd_vif_cfg),
				    UMAC_CMD_VIF_CFG);
}


int uccp420wlan_prog_vif_op_channel(int index,
				    unsigned char *vif_addr,
				    unsigned char op_channel)
{

	struct cmd_vif_cfg vif_cfg;

	memset(&vif_cfg, 0, sizeof(struct cmd_vif_cfg));
	vif_cfg.changed_bitmap = OP_CHAN_CHANGED;
	vif_cfg.op_channel = op_channel;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp420wlan_send_cmd((unsigned char *)&vif_cfg,
				    sizeof(struct cmd_vif_cfg),
				    UMAC_CMD_VIF_CFG);
}


int uccp420wlan_prog_vif_conn_state(int index,
				       unsigned char *vif_addr,
				       unsigned int connect_state)
{

	struct cmd_vif_cfg vif_cfg;

	memset(&vif_cfg, 0, sizeof(struct cmd_vif_cfg));
	vif_cfg.changed_bitmap = CONNECT_STATE_CHANGED;
	vif_cfg.connect_state = connect_state;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);
	return uccp420wlan_send_cmd((unsigned char *)&vif_cfg,
				    sizeof(struct cmd_vif_cfg),
				    UMAC_CMD_VIF_CFG);
}


int uccp420wlan_prog_vif_assoc_cap(int index,
				   unsigned char *vif_addr,
				   unsigned int caps)
{
	struct cmd_vif_cfg vif_cfg;


	memset(&vif_cfg, 0, sizeof(struct cmd_vif_cfg));
	vif_cfg.changed_bitmap = CAPABILITY_CHANGED;
	vif_cfg.capability = caps;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp420wlan_send_cmd((unsigned char *)&vif_cfg,
				    sizeof(struct cmd_vif_cfg),
				    UMAC_CMD_VIF_CFG);

}


int uccp420wlan_prog_vif_beacon_int(int index,
				    unsigned char *vif_addr,
				    unsigned int bcn_int)
{
	struct cmd_vif_cfg vif_cfg;

	memset(&vif_cfg, 0, sizeof(struct cmd_vif_cfg));

	vif_cfg.changed_bitmap = BCN_INT_CHANGED;
	vif_cfg.beacon_interval = bcn_int;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp420wlan_send_cmd((unsigned char *)&vif_cfg,
				    sizeof(struct cmd_vif_cfg),
				    UMAC_CMD_VIF_CFG);
}


int uccp420wlan_prog_vif_dtim_period(int index,
				     unsigned char *vif_addr,
				     unsigned int dtim_period)
{
	struct cmd_vif_cfg vif_cfg;

	memset(&vif_cfg, 0, sizeof(struct cmd_vif_cfg));

	vif_cfg.changed_bitmap = DTIM_PERIOD_CHANGED;
	vif_cfg.beacon_interval = dtim_period;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	return uccp420wlan_send_cmd((unsigned char *)&vif_cfg,
				    sizeof(struct cmd_vif_cfg),
				    UMAC_CMD_VIF_CFG);
}


int uccp420wlan_prog_vif_bssid(int index,
			       unsigned char *vif_addr,
			       unsigned char *bssid)
{
	struct cmd_vif_cfg vif_cfg;

	memset(&vif_cfg, 0, sizeof(struct cmd_vif_cfg));
	vif_cfg.changed_bitmap = BSSID_CHANGED;
	memcpy(vif_cfg.bssid, bssid, 6);
	memcpy(vif_cfg.vif_addr, vif_addr, 6);
	vif_cfg.if_index = index;

	return uccp420wlan_send_cmd((unsigned char *)&vif_cfg,
				    sizeof(struct cmd_vif_cfg),
				    UMAC_CMD_VIF_CFG);
}


int uccp420wlan_prog_vif_smps(int index,
			      unsigned char *vif_addr,
			      unsigned char smps_mode)
{
	struct cmd_vif_cfg vif_cfg;

	memset(&vif_cfg, 0, sizeof(struct cmd_vif_cfg));
	vif_cfg.changed_bitmap = SMPS_CHANGED;
	vif_cfg.if_index = index;
	memcpy(vif_cfg.vif_addr, vif_addr, 6);

	switch (smps_mode) {
	case IEEE80211_SMPS_STATIC:
		vif_cfg.smps_info |= SMPS_ENABLED;
		break;
	case IEEE80211_SMPS_DYNAMIC:
		vif_cfg.smps_info |= SMPS_ENABLED;
		vif_cfg.smps_info |= SMPS_MODE;
		break;
	case IEEE80211_SMPS_AUTOMATIC:/* will be one of the above*/
	case IEEE80211_SMPS_OFF:
		break;
	default:
		WARN(1, "Invalid SMPS Mode: %d\n", smps_mode);
	}

	return uccp420wlan_send_cmd((unsigned char *)&vif_cfg,
				    sizeof(struct cmd_vif_cfg),
				    UMAC_CMD_VIF_CFG);
}


int uccp420wlan_sta_add(int index, struct peer_sta_info *st)
{
	struct cmd_sta sta;
	int i;

	memset(&sta, 0, (sizeof(struct cmd_sta)));
	sta.op = ADD;

	for (i = 0; i < STA_NUM_BANDS; i++)
		sta.supp_rates[i] = st->supp_rates[i];

	/* HT info */
	sta.if_index = index;
	sta.ht_cap = st->ht_cap;
	sta.ht_supported = st->ht_supported;
	sta.vht_supported = st->vht_supported;
	sta.vht_cap = st->vht_cap;
	sta.ampdu_factor = st->ampdu_factor;
	sta.ampdu_density = st->ampdu_density;
	sta.rx_highest = st->rx_highest;
	sta.tx_params = st->tx_params;

	/* Enable it when FW supports it */
	/* sta.uapsd_queues = st->uapsd_queues; */
	for (i = 0; i < HT_MCS_MASK_LEN; i++)
		sta.rx_mask[i] = st->rx_mask[i];

	for (i = 0; i < ETH_ALEN; i++)
		sta.addr[i] = st->addr[i];

	return uccp420wlan_send_cmd((unsigned char *)&sta,
				    sizeof(struct cmd_sta), UMAC_CMD_STA);
}


int uccp420wlan_sta_remove(int index, struct peer_sta_info *st)
{
	struct cmd_sta sta;
	int i;

	memset(&sta, 0, (sizeof(struct cmd_sta)));
	sta.op = REM;

	for (i = 0; i < ETH_ALEN; i++)
		sta.addr[i] = st->addr[i];

	return uccp420wlan_send_cmd((unsigned char *)&sta,
				    sizeof(struct cmd_sta), UMAC_CMD_STA);

}


int uccp420wlan_prog_txq_params(int index,
				unsigned char *addr,
				unsigned int queue,
				unsigned int aifs,
				unsigned int txop,
				unsigned int cwmin,
				unsigned int cwmax,
				unsigned int uapsd)
{
	struct cmd_txq_params params;

	memset(&params, 0, (sizeof(struct cmd_txq_params)));

	params.if_index = index;
	ether_addr_copy(params.vif_addr, addr);
	params.queue_num = queue;
	params.aifsn = aifs;
	params.txop = txop;
	params.cwmin = cwmin;
	params.cwmax = cwmax;
	params.uapsd = uapsd;

	return uccp420wlan_send_cmd((unsigned char *) &params,
				    sizeof(struct cmd_txq_params),
				    UMAC_CMD_TXQ_PARAMS);
}


int uccp420wlan_set_rate(int rate, int mcs)
{
	struct cmd_rate cmd_rate;

	memset(&cmd_rate, 0, (sizeof(struct cmd_rate)));
	DEBUG_LOG("mcs = %d rate = %d\n", mcs, rate);
	cmd_rate.is_mcs = mcs;
	cmd_rate.rate = rate;
	return uccp420wlan_send_cmd((unsigned char *) &cmd_rate,
				    sizeof(struct cmd_rate),
				    UMAC_CMD_RATE);
}


int uccp420wlan_prog_rcv_bcn_mode(unsigned int bcn_rcv_mode)
{
	struct cmd_vif_cfg vif_cfg;

	memset(&vif_cfg, 0, sizeof(struct cmd_vif_cfg));
	vif_cfg.changed_bitmap = RCV_BCN_MODE_CHANGED;
	vif_cfg.bcn_mode = bcn_rcv_mode;

	return uccp420wlan_send_cmd((unsigned char *)&vif_cfg,
				    sizeof(struct cmd_vif_cfg),
				    UMAC_CMD_VIF_CFG);

}

int uccp420wlan_prog_aux_adc_chain(unsigned int chain_id)
{
	struct cmd_aux_adc_chain_sel aadc_chain_sel;

	memset(&aadc_chain_sel, 0, sizeof(struct cmd_aux_adc_chain_sel));
	aadc_chain_sel.chain_id = chain_id;

	return uccp420wlan_send_cmd((unsigned char *)&aadc_chain_sel,
				    sizeof(struct cmd_aux_adc_chain_sel),
				    UMAC_CMD_AUX_ADC_CHAIN_SEL);
}


int uccp420wlan_prog_mib_stats(void)
{
	struct host_mac_msg_hdr mib_stats_cmd;

	DEBUG_LOG("cmd mib stats\n");
	memset(&mib_stats_cmd, 0, sizeof(struct host_mac_msg_hdr));

	return uccp420wlan_send_cmd((unsigned char *)&mib_stats_cmd,
				    sizeof(struct host_mac_msg_hdr),
				    UMAC_CMD_MIB_STATS);
}


int uccp420wlan_prog_clear_stats(void)
{
	struct host_mac_msg_hdr clear_stats_cmd;

	DEBUG_LOG("cmd clear stats\n");
	memset(&clear_stats_cmd, 0, sizeof(struct host_mac_msg_hdr));

	return uccp420wlan_send_cmd((unsigned char *)&clear_stats_cmd,
				    sizeof(struct host_mac_msg_hdr),
				    UMAC_CMD_CLEAR_STATS);
}


int uccp420wlan_prog_phy_stats(void)
{
	struct host_mac_msg_hdr phy_stats_cmd;

	DEBUG_LOG("cmd phy stats\n");
	memset(&phy_stats_cmd, 0, sizeof(struct host_mac_msg_hdr));

	return uccp420wlan_send_cmd((unsigned char *)&phy_stats_cmd,
				    sizeof(struct host_mac_msg_hdr),
				    UMAC_CMD_PHY_STATS);
}


int uccp420wlan_prog_global_cfg(unsigned int rx_msdu_lifetime,
				unsigned int tx_msdu_lifetime,
				unsigned int sensitivity,
				unsigned int dyn_ed_enable,
				unsigned char *rf_params)
{
	/*DUMMY*/
	return 0;
}


#ifdef CONFIG_PM
int uccp420wlan_prog_econ_ps_state(int if_index,
				   unsigned int ps_state)
{
	struct cmd_ps ps_cfg;

	memset(&ps_cfg, 0, sizeof(struct cmd_ps));
	ps_cfg.mode = ps_state;
	ps_cfg.if_index = if_index;

	return uccp420wlan_send_cmd((unsigned char *)&ps_cfg,
				    sizeof(struct cmd_ps),
				    UMAC_CMD_PS_ECON_CFG);
}
#endif


int uccp420wlan_msg_handler (void *nbuff,
			     unsigned char sender_id)
{
	unsigned int event;
	unsigned char *buff;
	struct host_mac_msg_hdr *hdr;
	struct lmac_if_data *p;
	struct sk_buff *skb = (struct sk_buff *)nbuff;
	struct sk_buff *pending_cmd;
	unsigned long irq_flags;
	struct mac80211_dev *dev;

	rcu_read_lock();

	p = (struct lmac_if_data *)(rcu_dereference(lmac_if));

	if (!p) {
		WARN_ON(1);
		dev_kfree_skb_any(skb);
		rcu_read_unlock();
		return 0;
	}

	buff = skb->data;
	hdr = (struct host_mac_msg_hdr *)buff;

	event = hdr->id & 0xffff;

	dev = (struct mac80211_dev *)p->context;

	/* DEBUG_LOG("%s-UMACIF: event %d received\n", p->name, event); */
	if (event == UMAC_EVENT_RESET_COMPLETE) {
		struct host_event_reset_complete *r =
				(struct host_event_reset_complete *)buff;

		uccp420wlan_reset_complete(r->version, p->context);
		spin_lock_irqsave(&cmd_info.control_path_lock, irq_flags);

		if (cmd_info.outstanding_ctrl_req == 0) {
			pr_err("%s-UMACIF: Unexpected: Spurious proc_done received. Ignoring and continuing.\n",
			       p->name);
		} else {
			cmd_info.outstanding_ctrl_req--;

			DEBUG_LOG("After DEC: outstanding cmd: %d\n",
				     cmd_info.outstanding_ctrl_req);
			pending_cmd = skb_dequeue(&cmd_info.outstanding_cmd);

			if (unlikely(pending_cmd != NULL)) {
				DEBUG_LOG("Send 1 outstanding cmd\n");
				hal_ops.send((void *)pending_cmd, HOST_MOD_ID,
					     UMAC_MOD_ID, 0);
				dev->stats->gen_cmd_send_count++;
			}
		}

		spin_unlock_irqrestore(&cmd_info.control_path_lock, irq_flags);
	} else if (event == UMAC_EVENT_SCAN_ABORT_COMPLETE) {
		dev->scan_abort_done = 1;
#ifdef CONFIG_PM
	} else if (event == UMAC_EVENT_PS_ECON_CFG_DONE) {
		struct umac_event_ps_econ_cfg_complete *econ_cfg_complete_data =
				(struct umac_event_ps_econ_cfg_complete *)buff;
		dev->econ_ps_cfg_stats.completed = 1;
		dev->econ_ps_cfg_stats.result = econ_cfg_complete_data->status;
	} else if (event == UMAC_EVENT_PS_ECON_WAKE) {
		struct umac_event_ps_econ_wake *econ_wake_data =
					(struct umac_event_ps_econ_wake *)buff;
		dev->econ_ps_cfg_stats.wake_trig = econ_wake_data->trigger;
#endif
	} else if (event == UMAC_EVENT_SCAN_COMPLETE) {
		uccp420wlan_scan_complete(p->context,
			(struct host_event_scanres *) buff,
			buff +  sizeof(struct host_event_scanres), skb->len);

	} else if (event == UMAC_EVENT_RX /* ||
		   event == EVENT_RX_MIC_FAILURE*/) {
		if (dev->params->production_test) {
			dev->stats->rx_packet_data_count++;
			dev_kfree_skb_any(skb);
		} else {
			uccp420wlan_rx_frame(skb, p->context);

		}

	} else if (event == UMAC_EVENT_TX_DONE) {
		if (dev->params->production_test &&
		    dev->params->start_prod_mode)
			uccp420wlan_proc_tx_complete((void *)buff,
						     p->context);
		else {
			/* Increment tx_done_recv_count to keep track of number
			 * of tx_done received do not count tx dones from host.
			 */
			dev->stats->tx_done_recv_count++;

			uccp420wlan_tx_complete((void *)buff,
						p->context);
		}

		cmd_info.tx_done_recv_count++;

	} else if (event == UMAC_EVENT_DISCONNECTED) {
		struct host_event_disconnect *dis =
			(struct host_event_disconnect *)buff;
		int i = 0;

		if (dis->reason_code == REASON_NW_LOST) {
			while (i < MAX_VIFS) {
				if (dev->vifs[i]) {
					if ((memcmp(dev->vifs[i]->addr,
						    dis->mac_addr,
						    ETH_ALEN)) == 0) {
						ieee80211_connection_loss(
								dev->vifs[i]);
						break;
					}
				}
				i++;
			}
		}

	} else if (event == UMAC_EVENT_MIB_STAT) {
		struct umac_event_mib_stats  *mib_stats =
			(struct umac_event_mib_stats *) buff;

		uccp420wlan_mib_stats(mib_stats, p->context);
	} else if (event == UMAC_EVENT_MAC_STATS) {
		struct umac_event_mac_stats  *mac_stats =
			(struct umac_event_mac_stats *) buff;

		uccp420wlan_mac_stats(mac_stats, p->context);
	} else if (event == UMAC_EVENT_NW_FOUND) {
		DEBUG_LOG("received event_found\n");
	} else if (event == UMAC_EVENT_PHY_STAT) {
		int i;
#ifdef DRIVER_DEBUG
		struct host_event_phy_stats *phy =
			(struct host_event_phy_stats *)buff;
#endif
		DEBUG_LOG("received phy stats event\n");
		DEBUG_LOG("phy stats are\n");

		for (i = 0; i < 32; i++)
			DEBUG_LOG("%x ", phy->phy_stats[i]);

		DEBUG_LOG("\n\n\n");
	} else if (event == UMAC_EVENT_NOA) {
		uccp420wlan_noa_event(FROM_EVENT_NOA, (void *)buff,
				      p->context, NULL);

	} else if (event == UMAC_EVENT_COMMAND_PROC_DONE) {
		/*struct host_event_command_complete *cmd =
		 * (struct host_event_command_complete*)buff;
		 */
		DEBUG_LOG("Received  PROC_DONE\n");

		spin_lock_irqsave(&cmd_info.control_path_lock, irq_flags);

		if (cmd_info.outstanding_ctrl_req == 0) {
			pr_err("%s-UMACIF: Unexpected: Spurious proc_done received. Ignoring and continuing\n",
			       p->name);
		} else {
			cmd_info.outstanding_ctrl_req--;

			DEBUG_LOG("After DEC: outstanding cmd: %d\n",
				     cmd_info.outstanding_ctrl_req);

			pending_cmd = skb_dequeue(&cmd_info.outstanding_cmd);

			if (unlikely(pending_cmd != NULL)) {
				DEBUG_LOG("Send 1 outstanding cmd\n");
				hal_ops.send((void *)pending_cmd, HOST_MOD_ID,
					     UMAC_MOD_ID, 0);
				dev->stats->gen_cmd_send_count++;
			}
		}
		spin_unlock_irqrestore(&cmd_info.control_path_lock, irq_flags);

	} else if (event == UMAC_EVENT_CH_PROG_DONE) {
		uccp420wlan_ch_prog_complete(event,
			(struct umac_event_ch_prog_complete *)buff, p->context);
	} else if (event == UMAC_EVENT_RF_CALIB_DATA) {
		struct umac_event_rf_calib_data  *rf_data = (void *) buff;

		uccp420wlan_rf_calib_data(rf_data, p->context);
#ifdef MULTI_CHAN_SUPPORT
	/* SDK: Need to see if this will work in tasklet context (due to
	 * scheduling latencies) */
	} else if (event == UMAC_EVENT_CHAN_SWITCH) {
		uccp420wlan_proc_ch_sw_event((void *)buff,
					     p->context);

#endif
	} else {
		pr_warn("%s: Unknown event received %d\n", __func__, event);
	}

	if (event != UMAC_EVENT_RX)
		dev_kfree_skb_any(skb);

	rcu_read_unlock();

	return 0;
}


int uccp420wlan_lmac_if_init(void *context, const char *name)
{
	struct lmac_if_data *p;

	DEBUG_LOG("%s-UMACIF: lmac_if init called\n", name);

	p = kzalloc(sizeof(struct lmac_if_data), GFP_KERNEL);

	if (!p)
		return -ENOMEM;

	p->name = (char *)name;
	p->context = context;
	hal_ops.register_callback(uccp420wlan_msg_handler, UMAC_MOD_ID);
	rcu_assign_pointer(lmac_if, p);
	skb_queue_head_init(&cmd_info.outstanding_cmd);
	spin_lock_init(&cmd_info.control_path_lock);
	cmd_info.outstanding_ctrl_req = 0;

	return 0;
}


void uccp420wlan_lmac_if_deinit(void)
{
	struct lmac_if_data *p;

	DEBUG_LOG("%s-UMACIF: Deinit called\n", lmac_if->name);

	p = rcu_dereference(lmac_if);
	rcu_assign_pointer(lmac_if, NULL);
	synchronize_rcu();
	kfree(p);
}


void uccp420_lmac_if_free_outstnding(void)
{

	struct sk_buff *skb;

	/* First free the outstanding commands, we are not sending
	 * anymore commands to the FW except RESET.
	 */
	while ((skb = __skb_dequeue(&cmd_info.outstanding_cmd)))
		dev_kfree_skb_any(skb);

	cmd_info.outstanding_ctrl_req = 0;
}
