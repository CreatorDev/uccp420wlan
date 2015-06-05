/*
 * File Name  : tx.c
 *
 * This file contains the source functions UMAC TX logic
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

#include "core.h"

#define TX_TO_MACDEV(x) ((struct mac80211_dev *) \
			 (container_of(x, struct mac80211_dev, tx)))

static void wait_for_tx_complete(struct tx_config *tx)
{
	int count = 0;
	struct mac80211_dev *dev = NULL;

	/* Find_last_bit: Returns the bit number of the first set bit,
	 * or size.
	 */
	while (find_last_bit(tx->buf_pool_bmp,
			     NUM_TX_DESCS) != NUM_TX_DESCS) {
		count++;

		if (count < TX_COMPLETE_TIMEOUT_TICKS) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
		} else {
			dev = TX_TO_MACDEV(tx);

			DEBUG_LOG("%s-UMACTX:WARNING: TX complete failed!!\n",
				dev->name);
			DEBUG_LOG("%s-UMACTX:After %ld: bitmap is: 0x%lx\n",
			       dev->name,
			       TX_COMPLETE_TIMEOUT_TICKS,
			       tx->buf_pool_bmp[0]);
			break;
		}
	}

	if (count && (count < TX_COMPLETE_TIMEOUT_TICKS)) {
		DEBUG_LOG("%s-UMACTX:TX complete after %d timer ticks\n",
			dev->name, count);
	}
}


static inline int tx_queue_map(int queue)
{
	unsigned int ac[4] = {WLAN_AC_VO, WLAN_AC_VI, WLAN_AC_BE, WLAN_AC_BK};

	if (queue < 4)
		return ac[queue];

	return WLAN_AC_VO;
}


static inline int tx_queue_unmap(int queue)
{
	unsigned int ac[4] = {3, 2, 1, 0};

	return ac[queue];
}

static void update_aux_adc_voltage(struct mac80211_dev *dev,
				   unsigned char pdout)
{
	static unsigned int index;

	if (index > MAX_AUX_ADC_SAMPLES)
		index = 0;

	dev->params->pdout_voltage[index++] = pdout;
}

static void tx_status(struct sk_buff *skb,
		      struct umac_event_tx_done *tx_done,
		      unsigned int frame_idx,
		      struct mac80211_dev *dev,
		      struct ieee80211_tx_info tx_info_1st_mpdu)
{
	int index, i;
	char idx = 0;
	struct ieee80211_tx_rate *txrate;
	struct ieee80211_tx_rate *tx_inf_rate = NULL;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	int tx_fixed_mcs_idx = 0;
	int tx_fixed_rate = 0;
	struct ieee80211_supported_band *band = NULL;

	/* Rate info will be retained, except the count*/
	ieee80211_tx_info_clear_status(tx_info);

	if (tx_done->frm_status[frame_idx] == TX_DONE_STAT_SUCCESS)
		tx_info->flags |= IEEE80211_TX_STAT_ACK;
	else if (tx_info->flags & IEEE80211_TX_CTL_AMPDU)
		tx_info->flags |= IEEE80211_TX_STAT_AMPDU_NO_BACK;

	tx_info->flags &= ~IEEE80211_TX_STAT_AMPDU;
	tx_info->flags &= ~IEEE80211_TX_CTL_AMPDU;

	band = dev->hw->wiphy->bands[tx_info->band];

	for (index = 0; index < 4; index++) {
		tx_inf_rate = &tx_info->status.rates[index];

		/* Populate tx_info based on 1st MPDU in an AMPDU */
		txrate = (&tx_info_1st_mpdu.control.rates[index]);

		if (txrate->idx < 0)
			break;

		if ((dev->params->production_test == 1) &&
		    ((dev->params->tx_fixed_mcs_indx != -1) ||
		     (dev->params->tx_fixed_rate != -1))) {
			tx_fixed_mcs_idx = dev->params->tx_fixed_mcs_indx;
			tx_fixed_rate = dev->params->tx_fixed_rate;

			/* This index is always zero */
			/* TODO: See if we need to send channel bw information
			 * taken from proc, since in Production mode the bw
			 * advised by Minstrel can be overwritten by proc
			 * settings
			 */
			tx_inf_rate->flags = txrate->flags;

			if (tx_fixed_mcs_idx != -1) {
				if (tx_fixed_mcs_idx <= 7) {
					tx_inf_rate->flags |=
						IEEE80211_TX_RC_MCS;
					/* So that actual sent rate is seen in
					 * sniffer
					 */
					idx = tx_done->rate[frame_idx] & 0x7F;
					tx_inf_rate->idx = idx;
				} else if (tx_fixed_mcs_idx <= 9) {
					tx_inf_rate->flags |=
						IEEE80211_TX_RC_VHT_MCS;
					/* So that actual sent rate is seen
					 * in sniffer
					 */
					idx = ((dev->params->num_spatial_streams
					       << 4) & 0xF0);
					idx |= (tx_done->rate[frame_idx] &
						0x0F);
					tx_inf_rate->idx = idx;
				}
			} else if (tx_fixed_rate != -1) {
				for (i = 0; i < band->n_bitrates; i++) {
					if ((band->bitrates[i]).hw_value ==
					    tx_done->rate[frame_idx])
						tx_inf_rate->idx = i;
				}
			}

			tx_inf_rate->count = (tx_done->retries_num[frame_idx] +
					      1);
			break;
		}

		if ((tx_done->rate[frame_idx] &
		     MARK_RATE_AS_MCS_INDEX) == 0x80) {
			if ((txrate->flags & IEEE80211_TX_RC_VHT_MCS) &&
			    ((tx_done->rate[frame_idx] & 0x0F) ==
			     (txrate->idx & 0x0F))) {
				tx_inf_rate->count =
					(tx_done->retries_num[frame_idx] + 1);
			} else if ((txrate->flags & IEEE80211_TX_RC_MCS) &&
				   ((tx_done->rate[frame_idx] & 0x7F) ==
				    (txrate->idx & 0x7F))) {
				tx_inf_rate->count =
					(tx_done->retries_num[frame_idx] + 1);
			}

			break;
		} else if (tx_done->rate[frame_idx] ==
			   (band->bitrates[tx_inf_rate->idx]).hw_value) {
			tx_inf_rate->count =
				(tx_done->retries_num[frame_idx] + 1);

			break;
		}
	}

	/* Invalidate the remaining indices */
	while (((index + 1) < 4)) {
		tx_info->status.rates[index + 1].idx = -1;
		tx_info->status.rates[index + 1].count = 0;
		index++;
	}

	if ((tx_info->flags & IEEE80211_TX_CTL_TX_OFFCHAN) &&
	    (atomic_dec_return(&dev->roc_params.roc_mgmt_tx_count) == 0)) {
		if (dev->roc_params.roc_in_progress) {
			/* Reuse the delayed workqueue with 1ms delay */
			ieee80211_queue_delayed_work(dev->hw,
						     &dev->roc_complete_work,
						     msecs_to_jiffies(1));
		}
	}
	dev->stats->tx_dones_to_stack++;
	ieee80211_tx_status(dev->hw, skb);
}


static int get_token(struct tx_config *tx,
		     int queue)
{
	int cnt = 0, spare_tid = NUM_TX_DESCS;

	for (cnt = 0; cnt < NUM_TX_DESCS_PER_AC; cnt++) {
		if (!test_and_set_bit((queue + (NUM_ACS * cnt)),
							&tx->buf_pool_bmp[0])) {

			spare_tid = queue + (NUM_ACS * cnt);
			break;
		}
	}
	if (spare_tid == NUM_TX_DESCS) {
		for (cnt = NUM_TX_DESCS_PER_AC * NUM_ACS;
					cnt < NUM_TX_DESCS; cnt++) {
			/* Do not set, we will queue to the same token */
			if (!test_and_set_bit((cnt%TX_DESC_BUCKET_BOUND),
					      &tx->buf_pool_bmp[
					      (cnt/TX_DESC_BUCKET_BOUND)])) {
				spare_tid = cnt;
				break;
			}
		}
	}

	if (spare_tid != NUM_TX_DESCS)
		tx->outstanding_tokens[queue]++;

	return spare_tid;
}


int uccp420wlan_tx_alloc_buff_req(struct mac80211_dev *dev,
				  int queue,
				  unsigned int *id,
				  struct sk_buff *skb)
{
	int spare = 0;
	struct tx_config *tx = &dev->tx;
	unsigned long flags, ampdu_len = 0;
	struct sk_buff *loop_skb;
	struct sk_buff *tmp, *skb_first = NULL;
	struct ieee80211_hdr *mac_hdr_first, *mac_hdr;
	struct ieee80211_tx_info *tx_info_first, *tx_info;
	unsigned int max_tx_cmds = dev->params->max_tx_cmds;
	struct umac_vif *uvif;
	struct ieee80211_vif *ivif;
	unsigned char *data = NULL;

	spin_lock_irqsave(&tx->lock, flags);

	DEBUG_LOG("%s-UMACTX:Alloc buf Req q = %d,\n", dev->name, queue);

	*id = NUM_TX_DESCS;

	if ((tx->outstanding_tokens[queue] < NUM_TX_DESCS_PER_AC)
	     || (queue == WLAN_AC_BCN)) {
		/* Reserved Full and Outstanding < 2*/
		spare = get_token(tx, queue);

		if (spare != NUM_TX_DESCS) {
			DEBUG_LOG("%s-UMACTX:Reserved Token, Sending single\n",
				dev->name);
			skb_queue_tail(&dev->tx.tx_pkt[spare], skb);
		}

		*id = spare;

		goto out;
	}

	skb_queue_tail(&tx->pending_pkt[queue], skb);

	if (skb_queue_len(&tx->pending_pkt[queue]) < max_tx_cmds) {
		*id = NUM_TX_DESCS;
		goto out;
	}

	/* Check Spare Token */
	spare = get_token(tx, queue);

	if (spare != NUM_TX_DESCS) {
		skb_first = skb_peek(&tx->pending_pkt[queue]);

		mac_hdr_first = (struct ieee80211_hdr *)skb_first->data;

		tx_info_first = IEEE80211_SKB_CB(skb_first);

		/* Temp Checks for Aggregation: Will be removed later*/
		if ((tx_info_first->control.rates[0].flags &
		     IEEE80211_TX_RC_VHT_MCS) && max_tx_cmds > 24)
			max_tx_cmds = 24;
		else if ((tx_info_first->control.rates[0].flags &
			  IEEE80211_TX_RC_MCS) && max_tx_cmds > 16)
			max_tx_cmds = 16;

		/* Aggregate Only MPDU's with same RA, same Rate,
		 * same Rate flags, same Tx Info flags
		 */
		skb_queue_walk_safe(&tx->pending_pkt[queue],
				    loop_skb,
				    tmp) {
			data = loop_skb->data;
			mac_hdr = (struct ieee80211_hdr *)data;

			tx_info = IEEE80211_SKB_CB(loop_skb);

			ivif = tx_info->control.vif;
			uvif = (struct umac_vif *)(ivif->drv_priv);

			ampdu_len += loop_skb->len;

			if (!ieee80211_is_data(mac_hdr->frame_control) ||
			    !(tx_info->flags & IEEE80211_TX_CTL_AMPDU) ||
			    (skb_queue_len(&dev->tx.tx_pkt[spare]) >=
			     max_tx_cmds) ||
#if 0
			    (memcmp(&tx_info_first->control.rates[0],
				    &tx_info->control.rates[0],
				    sizeof(struct ieee80211_tx_rate) *
				    IEEE80211_TX_MAX_RATES) != 0) ||
			    (tx_info_first->flags != tx_info->flags) ||
#endif
			    (memcmp(mac_hdr->addr1,
				    mac_hdr_first->addr1,
				    ETH_ALEN) == 0))
				break;

			__skb_unlink(loop_skb, &tx->pending_pkt[queue]);

			skb_queue_tail(&dev->tx.tx_pkt[spare], loop_skb);
		}

		/* If our criterion rejects all pending frames, send only 1 */
		if (!skb_queue_len(&dev->tx.tx_pkt[spare]))
			skb_queue_tail(&dev->tx.tx_pkt[spare],
				       skb_dequeue(&tx->pending_pkt[queue]));

		DEBUG_LOG("%s-UMACTX:Max_pkt_thresh: send spare: %d with %d\n",
		       dev->name,
		       spare,
		       skb_queue_len(&dev->tx.tx_pkt[spare]));

		/* We can send only list here, but when the count
		 * reaches 32 again we send another
		 */
	}

	/* No spare token, so make sure queue is not overflowing */
	if ((queue != WLAN_AC_BCN) &&
	    (skb_queue_len(&tx->pending_pkt[queue]) >= MAX_TX_QUEUE_LEN)) {
		ieee80211_stop_queue(dev->hw,
				     skb->queue_mapping);
		tx->queue_stopped_bmp |= (1 << queue);
	}

	*id = spare;

out:
	DEBUG_LOG("%s-UMACTX:Alloc buf Result *id = %d\n", dev->name, *id);

	spin_unlock_irqrestore(&tx->lock, flags);

	/* If token is available, just return tokenid, list will be sent*/
	return *id;
}


int uccp420wlan_tx_free_buff_req(struct mac80211_dev *dev,
				 struct umac_event_tx_done *tx_done,
				 unsigned char *queue,
				 int *vif_index_bitmap)
{
	int i;
	unsigned long flags, ampdu_len;
	unsigned int pkts_pend = 0;
	struct tx_config *tx = &dev->tx;
	struct ieee80211_hdr *mac_hdr_first, *mac_hdr;
	struct ieee80211_tx_info *tx_info_first, *tx_info, *tx_info_bcn;
	struct ieee80211_tx_info tx_info_1st_mpdu;
	struct sk_buff *skb, *tmp, *skb_first = NULL;
	struct sk_buff_head *skb_list, tx_done_list;
	int vif_index;
	unsigned int pkt = 0, cnt = 0;
	unsigned int descriptor_id = tx_done->descriptor_id;
	unsigned int max_tx_cmds = dev->params->max_tx_cmds;
	struct umac_vif *uvif;
	struct ieee80211_vif *ivif;
	unsigned long bcn_int = 0;

	skb_queue_head_init(&tx_done_list);

	DEBUG_LOG("%s-UMACTX:Free buf Req q = %d, desc_id: %d\n",
	       dev->name,
	       tx_done->queue,
	       descriptor_id);

	spin_lock_irqsave(&tx->lock, flags);
	tx->outstanding_tokens[tx_done->queue]--;

	for (i = 0; i < NUM_ACS; i++) {
		if (skb_peek(&tx->pending_pkt[i]))
			break;
	}

	if (i == NUM_ACS) {
		/* No pending packets */
		__clear_bit((descriptor_id % TX_DESC_BUCKET_BOUND),
			    &tx->buf_pool_bmp[(descriptor_id /
					       TX_DESC_BUCKET_BOUND)]);
	} else if (descriptor_id < (NUM_TX_DESCS_PER_AC * NUM_ACS)) {
		/* Reserved token */
		*queue = tx_done->queue;
		if (*queue != WLAN_AC_BCN) {
			pkts_pend = skb_queue_len(&tx->pending_pkt[*queue]);

			if (!pkts_pend)
				__clear_bit(descriptor_id,
					    &tx->buf_pool_bmp[0]);
		} else {
			__clear_bit(descriptor_id, &tx->buf_pool_bmp[0]);
		}
	} else if (descriptor_id >= (NUM_TX_DESCS_PER_AC * NUM_ACS)) {
		/* Spare token */
		for (cnt = WLAN_AC_VO; cnt >= 0; cnt--) {
			pkts_pend = skb_queue_len(&tx->pending_pkt[cnt]);

			if (pkts_pend) {
				*queue = cnt;
				break;
			}
		}

		/* If beacon queue has pending and
		 * no other AC has pending
		 */
		if (!pkts_pend) {
			__clear_bit((descriptor_id %
				     TX_DESC_BUCKET_BOUND),
				    &tx->buf_pool_bmp[(descriptor_id /
						       TX_DESC_BUCKET_BOUND)]);
			}
	}

	DEBUG_LOG("%s-UMACTX:%spending_q = %d, desc_id: %d pending:%d\n",
	       dev->name,
	       __func__,
	       *queue,
	       descriptor_id,
	       pkts_pend);

	/* Defer Tx Done Processsing */
	skb_list = &dev->tx.tx_pkt[descriptor_id];

	if (skb_queue_len(skb_list)) {
		/* Cut the list to new one, tx_pkt will be re-initialized */
		skb_queue_splice_tail_init(skb_list, &tx_done_list);
	} else {
		DEBUG_LOG("%s-UMACTX:Got Empty List: list_addr: %p\n",
			dev->name, skb_list);
	}

	if (pkts_pend > 0) {
		skb_first = skb_peek(&tx->pending_pkt[*queue]);
		mac_hdr_first = (struct ieee80211_hdr *)skb_first->data;
		tx_info_first = IEEE80211_SKB_CB(skb_first);

		/* Temp Checks for Aggregation: Will be removed later */
		if ((tx_info_first->control.rates[0].flags &
		     IEEE80211_TX_RC_VHT_MCS) && max_tx_cmds > 24)
			max_tx_cmds = 24;
		else if ((tx_info_first->control.rates[0].flags &
			  IEEE80211_TX_RC_MCS) && max_tx_cmds > 16)
			max_tx_cmds = 16;

		skb_queue_walk_safe(&tx->pending_pkt[*queue], skb, tmp) {
			mac_hdr = (struct ieee80211_hdr *)skb->data;
			tx_info =
			     (struct ieee80211_tx_info *)IEEE80211_SKB_CB(skb);

			ivif = tx_info->control.vif;
			uvif = (struct umac_vif *)(ivif->drv_priv);
			ampdu_len += skb->len;

			/* Aggregate Only AMPDU's with same RA, same Rate,
			 * same Rate Falgs, same Tx info flags
			 */
			if (!ieee80211_is_data(mac_hdr->frame_control) ||
			    !(tx_info->flags & IEEE80211_TX_CTL_AMPDU) ||
			    skb_queue_len(skb_list) >= max_tx_cmds ||
#if 0
			    (memcmp(&tx_info_first->control.rates[0],
				    &tx_info->control.rates[0],
				    sizeof(struct ieee80211_tx_rate) *
				    IEEE80211_TX_MAX_RATES) != 0) ||
			    tx_info_first->flags != tx_info->flags ||
#endif
			    (memcmp(mac_hdr->addr1,
				    mac_hdr_first->addr1,
				    ETH_ALEN) != 0))
					break;

			/*Always queue the first skb*/
			__skb_unlink(skb, &tx->pending_pkt[*queue]);
			skb_queue_tail(skb_list, skb);
		}

		/* If our criterion rejects all pending frames, send only 1 */
		if (!skb_queue_len(skb_list)) {
			skb_queue_tail(skb_list,
				       skb_dequeue(&tx->pending_pkt[*queue]));
		}

		tx->outstanding_tokens[*queue]++;

		DEBUG_LOG("%s-UMACTX:Pending packets: %d, Total: %d\n",
		       dev->name,
		       pkts_pend,
		       skb_queue_len(skb_list));
	} else {
		DEBUG_LOG("%s-UMACTX:No Pending Packets\n", dev->name);
	}

	if ((*queue != WLAN_AC_BCN) &&
	    (tx->queue_stopped_bmp & (1 << *queue)) &&
	    skb_queue_len(&tx->pending_pkt[*queue]) < (MAX_TX_QUEUE_LEN / 2)) {
		ieee80211_wake_queue(dev->hw, tx_queue_unmap(*queue));
		tx->queue_stopped_bmp &= ~(1 << (*queue));
	}
	/*Unmap here before release lock to avoid race*/
	if (skb_queue_len(&tx_done_list)) {
		skb_queue_walk_safe(&tx_done_list, skb, tmp) {
			hal_ops.unmap_tx_buf(tx_done->descriptor_id, pkt);
			DEBUG_LOG("%s-UMACTX:TXDONE: ID=%d, Stat=%d (%d, %d)\n",
				dev->name,
				tx_done->descriptor_id,
				tx_done->frm_status[pkt],
				tx_done->rate[pkt],
				tx_done->retries_num[pkt]);
			pkt++;
		}
	}
	/*Unlock: Give a chance for Tx to add to pending lists*/
	spin_unlock_irqrestore(&tx->lock, flags);

	/* Protection from mac80211 _ops especially stop */
	if (dev->state != STARTED)
		return 0;

	if (!skb_queue_len(&tx_done_list))
		goto out;

	skb_first = skb_peek(&tx_done_list);

	memcpy(&tx_info_1st_mpdu,
	       (struct ieee80211_tx_info *)IEEE80211_SKB_CB(skb_first),
	       sizeof(struct ieee80211_tx_info));
	pkt = 0;
	skb_queue_walk_safe(&tx_done_list, skb, tmp) {
		__skb_unlink(skb, &tx_done_list);

		if (!skb)
			continue;
		/* In the Tx path we move the .11hdr from skb to CMD_TX
		 * Hence pushing it here, not required for loopback case
		 */
		skb_push(skb,
			 dev->tx.tx_pkt_hdr_len[tx_done->descriptor_id]);
		mac_hdr = (struct ieee80211_hdr *)(skb->data);

		if (!ieee80211_is_beacon(mac_hdr->frame_control)) {
			vif_index = vif_addr_to_index(mac_hdr->addr2,
						      dev);
			if (vif_index > -1)
				*vif_index_bitmap |= (1 << vif_index);

			/* Same Rate info for all packets */
			tx_status(skb,
				  tx_done,
				  pkt,
				  dev,
				  tx_info_1st_mpdu);
		} else {

			if (tx_done->frm_status[pkt] ==
			    TX_DONE_STAT_DISCARD_BCN) {
				/*We did not send beacon*/
				dev->tx_last_beacon = 0;
			} else if (tx_done->frm_status[pkt] ==
				   TX_DONE_STAT_SUCCESS) {
				/*We did send beacon*/
				dev->tx_last_beacon = 1;
			}

			tx_info_bcn = IEEE80211_SKB_CB(skb);
			ivif = tx_info_bcn->control.vif;
			uvif = (struct umac_vif *)(ivif->drv_priv);
			bcn_int = uvif->vif->bss_conf.beacon_int - 10;
			bcn_int = msecs_to_jiffies(bcn_int);

			/* Beacon Time Stamp */
			if (tx_done->frm_status[pkt] == TX_DONE_STAT_SUCCESS) {
				unsigned int ts2;
				int bts_vif = uvif->vif_index;
				spin_lock(&tsf_lock);
				dev->params->sync[bts_vif].status = 1;
				memcpy(dev->params->sync[bts_vif].bssid,
					ivif->bss_conf.bssid, ETH_ALEN);
				memcpy(dev->params->sync[bts_vif].ts1,
					tx_done->reserved, 8);
				memcpy(&dev->params->sync[bts_vif].ts2,
					(tx_done->reserved + 8), 4);
				ts2 = dev->params->sync[bts_vif].ts2;
				dev->params->sync[bts_vif].atu = 0;
				if (frc_to_atu)
					frc_to_atu(ts2,
					&dev->params->sync[bts_vif].atu, 0);
				spin_unlock(&tsf_lock);
			}

			for (i = 0; i < MAX_VIFS; i++) {
				if (dev->active_vifs & (1 << i)) {
					if (dev->vifs[i] == ivif)
						mod_timer(&uvif->bcn_timer,
							  jiffies +
							  bcn_int);
				}
			}

			dev_kfree_skb_any(skb);
		}

		pkt++;
	}
out:
	return min(pkts_pend, max_tx_cmds);
}


#ifdef PERF_PROFILING
static void print_persec_stats(unsigned long data)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)data;
	struct tx_config *tx = &dev->tx;

	if (dev->stats->tx_cmds_from_stack != 0) {
		pr_info("%s: %d The persec stats from stack: %d outstanding_tokens: [%d = %d = %d = %d = %d]\n",
			__func__,
			__LINE__,
			dev->stats->tx_cmds_from_stack,
			tx->outstanding_tokens[0],
			tx->outstanding_tokens[1],
			tx->outstanding_tokens[2],
			tx->outstanding_tokens[3],
			tx->outstanding_tokens[4]);

		dev->stats->tx_cmds_from_stack = 0;
	}

	mod_timer(&tx->persec_timer, jiffies + msecs_to_jiffies(1000));
}
#endif


void uccp420wlan_tx_init(struct mac80211_dev *dev)
{
	int cnt = 0;
	struct tx_config *tx = &dev->tx;

	memset(&tx->buf_pool_bmp,
	       0,
	       sizeof(long) * ((NUM_TX_DESCS/TX_DESC_BUCKET_BOUND) + 1));

	tx->queue_stopped_bmp = 0;
	tx->next_spare_token_ac = WLAN_AC_BE;

	for (cnt = 0; cnt < NUM_ACS; cnt++) {
		skb_queue_head_init(&tx->pending_pkt[cnt]);
		tx->outstanding_tokens[cnt] = 0;
	}

	for (cnt = 0; cnt < NUM_TX_DESCS; cnt++)
		skb_queue_head_init(&tx->tx_pkt[cnt]);

#ifdef PERF_PROFILING
	init_timer(&tx->persec_timer);
	tx->persec_timer.data = (unsigned long)dev;
	tx->persec_timer.function = print_persec_stats;
	mod_timer(&tx->persec_timer, jiffies + msecs_to_jiffies(1000));
#endif
	spin_lock_init(&tx->lock);
	ieee80211_wake_queues(dev->hw);

	DEBUG_LOG("%s-UMACTX:Initialization successful\n", dev->name);
}


void uccp420wlan_tx_deinit(struct mac80211_dev *dev)
{
	int cnt = 0;
	unsigned long flags = 0;
	struct tx_config *tx = &dev->tx;
	struct sk_buff *skb;
	ieee80211_stop_queues(dev->hw);

	wait_for_tx_complete(tx);

	spin_lock_irqsave(&tx->lock, flags);

	for (cnt = 0; cnt < NUM_TX_DESCS; cnt++) {
		while ((skb = skb_dequeue(&tx->tx_pkt[cnt])) != NULL)
			dev_kfree_skb_any(skb);
	}

	for (cnt = 0; cnt < NUM_ACS; cnt++) {
		while ((skb = skb_dequeue(&tx->pending_pkt[cnt])) != NULL)
			dev_kfree_skb_any(skb);
	}

	spin_unlock_irqrestore(&tx->lock, flags);

	DEBUG_LOG("%s-UMACTX:Deinitialization successful\n", dev->name);
}


int __uccp420wlan_tx_frame(struct ieee80211_sta *sta,
			   unsigned int queue,
			   unsigned int buff_pool_id,
			   unsigned int more_frames,
			   struct mac80211_dev *dev)
{
	return uccp420wlan_prog_tx(queue, more_frames, buff_pool_id);
}


int uccp420wlan_tx_frame(struct sk_buff *skb,
			 struct ieee80211_sta *sta,
			 struct mac80211_dev *dev,
			 bool bcast)
{
	unsigned int queue, descriptor_id, pkt, more_frames;
	int ret = 0;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);

	if (bcast == false) {
		queue = tx_queue_map(skb->queue_mapping);
		more_frames = 0;
	} else {
		queue = WLAN_AC_BCN;
		/* Hack: skb->priority is used to indicate more frames */
		more_frames = skb->priority;
	}

	dev->stats->tx_cmds_from_stack++;

	if (dev->params->production_test == 1)
		tx_info->flags |= IEEE80211_TX_CTL_AMPDU;

	if (tx_info->flags & IEEE80211_TX_CTL_TX_OFFCHAN) {
		/*These are high priority frames, send them in VO*/
		queue = WLAN_AC_VO;
		atomic_inc(&dev->roc_params.roc_mgmt_tx_count);
	}

	DEBUG_LOG("%s-UMACTX:%s:%d Waiting for Allocation:queue: %d qmap: %d\n",
		dev->name,
		__func__, __LINE__, queue, skb->queue_mapping);

	uccp420wlan_tx_alloc_buff_req(dev, queue, &descriptor_id, skb);

	if (descriptor_id == NUM_TX_DESCS) {
		DEBUG_LOG("%s-UMACTX:%s:%d Token Busy Queued:\n",
			dev->name, __func__, __LINE__);
		return NETDEV_TX_OK;
	}
	ret = __uccp420wlan_tx_frame(sta, queue, descriptor_id, more_frames,
				     dev);
	if (ret < 0) {
		struct umac_event_tx_done tx_done;

		pr_err("%s-UMACTX: Unable to send frame, dropping ..%d\n",
		       dev->name, ret);

		tx_done.descriptor_id = descriptor_id;
		tx_done.queue = queue;

		for (pkt = 0; pkt <
		     skb_queue_len(&dev->tx.tx_pkt[descriptor_id]); pkt++) {
			tx_done.frm_status[pkt] =
				TX_DONE_STAT_ERR_RETRY_LIM;
			tx_done.rate[pkt] = 0;
		}

		uccp420wlan_tx_complete(&tx_done, dev);
	}

	return NETDEV_TX_OK;
}


void uccp420wlan_proc_tx_complete(struct umac_event_tx_done *tx_done,
			     void *context)
{

	struct mac80211_dev *dev = (struct mac80211_dev *)context;
	struct sk_buff *skb, *tmp;
	struct sk_buff_head *tx_done_list;
	unsigned int pkt = 0;

	tx_done_list = &dev->tx.proc_tx_list[tx_done->descriptor_id];
	dev->stats->tx_done_recv_count++;
	update_aux_adc_voltage(dev, tx_done->pdout_voltage);
	skb_queue_walk_safe(tx_done_list, skb, tmp) {
		__skb_unlink(skb, tx_done_list);
		if (!skb)
			continue;
		hal_ops.unmap_tx_buf(tx_done->descriptor_id, pkt);
		dev_kfree_skb_any(skb);
		pkt++;
	}

	/*send NEXT packet list*/
	if ((dev->params->pkt_gen_val == -1) ||
	    (--dev->params->pkt_gen_val > 0))
		tasklet_schedule(&dev->proc_tx_tasklet);
}

void uccp420wlan_tx_complete(struct umac_event_tx_done *tx_done,
			     void *context)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)context;
	unsigned int  more_frames;
	int vif_index = 0, vif_index_bitmap = 0, ret = 0;
	unsigned int pkt = 0, pkts_pending = 0;
	unsigned char queue = 0;
	struct umac_event_noa noa_event;

	/*for (i = 0; i < 32; i++)*/
	{
		/* increment tx_done_recv_count to keep track of number of
		 * tx_done received do not count tx dones from host.
		 */
		dev->stats->tx_done_recv_count++;
tx_complete:

		DEBUG_LOG("%s-UMACTX:TX Done Rx for desc_id: %d qlen: %d\n",
		       dev->name,
		       tx_done->descriptor_id,
		       skb_queue_len(&dev->tx.tx_pkt[
				     tx_done->descriptor_id]));
		update_aux_adc_voltage(dev, tx_done->pdout_voltage);
		pkts_pending = uccp420wlan_tx_free_buff_req(dev,
							    tx_done,
							    &queue,
							    &vif_index_bitmap);


		if (pkts_pending) {
			/*TODO..Do we need to check each skb for more_frames??*/
#if 0
			if ((queue == WLAN_AC_BCN) && (skb->priority == 1))
				more_frames = 1;
			else
				more_frames = 0;
#endif
			more_frames = 0;

			DEBUG_LOG("%s-UMACTX:%s:%d Transfer Pending Frames:\n",
			       dev->name,
			       __func__, __LINE__);

			ret = __uccp420wlan_tx_frame(NULL,
						     queue,
						     tx_done->descriptor_id,
						     more_frames, dev);

			if (ret < 0) {
				DEBUG_LOG("%s-UMACTX:TX (pending) failed %d\n",
				       dev->name,
				       ret);

				tx_done->queue = queue;

				for (pkt = 0; pkt < pkts_pending; pkt++) {
					tx_done->frm_status[pkt] =
						TX_DONE_STAT_ERR_RETRY_LIM;
					tx_done->rate[pkt] = 0;
				}

				goto tx_complete;
			}
		} else {
			DEBUG_LOG("%s-UMACTX:No Pending Packets\n", dev->name);
		}
	}

	for (vif_index = 0; vif_index < MAX_VIFS; vif_index++) {
		if (vif_index_bitmap & (1 << vif_index)) {
			memset(&noa_event, 0, sizeof(noa_event));
			noa_event.if_index = vif_index;
			uccp420wlan_noa_event(FROM_TX_DONE,
					      &noa_event,
					      (void *)dev,
					      NULL);
		}
	}
}
