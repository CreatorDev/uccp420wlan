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


static int get_token(struct mac80211_dev *dev,
		     int queue)
{
	int cnt = 0;
	int curr_bit = 0;
	int pool_id = 0;
	int token_id = NUM_TX_DESCS;
	struct tx_config *tx = &dev->tx;

	/* First search for a reserved token */
	for (cnt = 0; cnt < NUM_TX_DESCS_PER_AC; cnt++) {
		if (!test_and_set_bit((queue + (NUM_ACS * cnt)),
							&tx->buf_pool_bmp[0])) {

			token_id = queue + (NUM_ACS * cnt);
			break;
		}
	}

	/* If reserved token is not found search for a spare token */
	if (cnt == NUM_TX_DESCS_PER_AC) {
		for (token_id = NUM_TX_DESCS_PER_AC * NUM_ACS;
		     token_id < NUM_TX_DESCS;
		     token_id++) {
			curr_bit = (token_id % TX_DESC_BUCKET_BOUND);
			pool_id = (token_id / TX_DESC_BUCKET_BOUND);
			/* Do not set, we will queue to the same token */
			if (!test_and_set_bit(curr_bit,
					      &tx->buf_pool_bmp[pool_id])) {
				break;
			}
		}
	}

	if (token_id != NUM_TX_DESCS) {
		tx->outstanding_tokens[queue]++;
#ifdef MULTI_CHAN_SUPPORT
		tx->desc_chan_map[token_id] = dev->curr_chanctx_idx;
#endif
	}

	return token_id;
}


#ifdef UNIFORM_BW_SHARING
int get_curr_peer_opp(struct mac80211_dev *dev,
		      int queue)
{
	unsigned int curr_peer_opp = 0;
	unsigned int i = 0;
	struct tx_config *tx = NULL;
#ifdef MULTI_CHAN_SUPPORT
	struct ieee80211_sta *sta = NULL;
	struct ieee80211_vif *vif = NULL;
	struct umac_sta *usta = NULL;
	struct umac_vif *uvif = NULL;
	int vif_index = -1;
#endif
	unsigned int init_peer_opp = 0;

	tx = &dev->tx;

#ifdef MULTI_CHAN_SUPPORT
	init_peer_opp = tx->curr_peer_opp[dev->curr_chanctx_idx][queue];
#else
	init_peer_opp = tx->curr_peer_opp[queue];
#endif

	for (i = 0; i < MAX_PEND_Q_PER_AC; i++) {
		curr_peer_opp = (init_peer_opp + i) % MAX_PEND_Q_PER_AC;

#ifdef MULTI_CHAN_SUPPORT
		rcu_read_lock();

		if (curr_peer_opp < MAX_PEERS) {
			sta = rcu_dereference(dev->peers[curr_peer_opp]);

			if (!sta) {
				rcu_read_unlock();
				continue;
			}

			usta = (struct umac_sta *)(sta->drv_priv);

			if (!usta->chanctx) {
				rcu_read_unlock();
				continue;
			}

			if (usta->chanctx->index != dev->curr_chanctx_idx) {
				rcu_read_unlock();
				continue;
			}

		} else {
			vif_index = (curr_peer_opp - MAX_PEERS);

			vif = rcu_dereference(dev->vifs[vif_index]);

			if (!vif) {
				rcu_read_unlock();
				continue;
			}

			uvif = (struct umac_vif *)(vif->drv_priv);

			if (!uvif->chanctx) {
				rcu_read_unlock();
				continue;
			}

			if (uvif->chanctx->index != dev->curr_chanctx_idx) {
				rcu_read_unlock();
				continue;
			}
		}

		rcu_read_unlock();
#endif

		if (skb_queue_len(&tx->pending_pkt[curr_peer_opp][queue])) {
#ifdef MULTI_CHAN_SUPPORT
			tx->curr_peer_opp[dev->curr_chanctx_idx][queue] =
				(curr_peer_opp + 1) % MAX_PEND_Q_PER_AC;
#else
			tx->curr_peer_opp[queue] =
				(curr_peer_opp + 1) % MAX_PEND_Q_PER_AC;
#endif
			break;
		}
	}

	if (i == MAX_PEND_Q_PER_AC)
		return -1;

	return curr_peer_opp;
}
#endif


void uccp420wlan_tx_proc_pend_frms(struct mac80211_dev *dev,
				   int queue,
#ifdef UNIFORM_BW_SHARING
				   int peer_id,
#endif
				   int token_id)
{
	struct tx_config *tx = &dev->tx;
	unsigned long ampdu_len = 0;
	struct sk_buff *loop_skb = NULL;
	struct sk_buff *tmp = NULL;
	struct sk_buff *skb_first = NULL;
	struct ieee80211_hdr *mac_hdr_first = NULL;
	struct ieee80211_hdr *mac_hdr = NULL;
	struct ieee80211_tx_info *tx_info_first = NULL;
	struct ieee80211_tx_info *tx_info = NULL;
	struct umac_vif *uvif = NULL;
	struct ieee80211_vif *ivif = NULL;
	unsigned char *data = NULL;
	unsigned int max_tx_cmds = dev->params->max_tx_cmds;
	struct sk_buff_head *txq = NULL;
	struct sk_buff_head *pend_pkt_q = NULL;
#ifdef MULTI_CHAN_SUPPORT
	int chanctx_idx = 0;
#endif

#ifdef UNIFORM_BW_SHARING
	pend_pkt_q = &tx->pending_pkt[peer_id][queue];
#else
	pend_pkt_q = &tx->pending_pkt[queue];
#endif

#ifdef MULTI_CHAN_SUPPORT
	chanctx_idx = dev->curr_chanctx_idx;

	txq = &dev->tx.pkt_info[chanctx_idx][token_id].pkt;
#else
	txq = &dev->tx.pkt_info[token_id].pkt;
#endif

	skb_first = skb_peek(pend_pkt_q);

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
	 * same Rate flags, same Tx Info flags */
	skb_queue_walk_safe(pend_pkt_q,
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
		    (skb_queue_len(txq) >= max_tx_cmds) ||
#if 0
		    (memcmp(&tx_info_first->control.rates[0],
			    &tx_info->control.rates[0],
			    sizeof(struct ieee80211_tx_rate) *
			    IEEE80211_TX_MAX_RATES) != 0) ||
		    (tx_info_first->flags != tx_info->flags) ||
#endif
		    (memcmp(mac_hdr->addr1,
			    mac_hdr_first->addr1,
			    ETH_ALEN) != 0))
			break;

		__skb_unlink(loop_skb, pend_pkt_q);

		skb_queue_tail(txq, loop_skb);
	}

	/* If our criterion rejects all pending frames, send only 1 */
	if (!skb_queue_len(txq))
		skb_queue_tail(txq, skb_dequeue(pend_pkt_q));

	DEBUG_LOG("%s-UMACTX:Max_pkt_thresh: send spare: %d with %d\n",
		  dev->name,
		  token_id,
		  skb_queue_len(txq));
}


int uccp420wlan_tx_alloc_buff_req(struct mac80211_dev *dev,
				  int queue,
#ifdef MULTI_CHAN_SUPPORT
				  struct umac_vif *uvif,
#endif
#ifdef UNIFORM_BW_SHARING
				  int peer_id,
#endif
				  struct sk_buff *skb)
{
	int token_id = NUM_TX_DESCS;
	struct tx_config *tx = &dev->tx;
	struct sk_buff_head *txq = NULL;
	unsigned long flags = 0;
	struct sk_buff_head *pend_pkt_q = NULL;
#ifdef UNIFORM_BW_SHARING
	int tx_peer_id = 0;
#endif
	struct ieee80211_hdr *mac_hdr = NULL;

	spin_lock_irqsave(&tx->lock, flags);

#ifdef UNIFORM_BW_SHARING
	pend_pkt_q = &tx->pending_pkt[peer_id][queue];
#else
	pend_pkt_q = &tx->pending_pkt[queue];
#endif

	DEBUG_LOG("%s-UMACTX:Alloc buf Req q = %d,\n", dev->name, queue);

#ifdef MULTI_CHAN_SUPPORT
	if (uvif->chanctx->index == dev->curr_chanctx_idx)
#endif
		token_id = get_token(dev, queue);

	/* If we got a reserved token, then queue frame to the Xmit queue */
	if (token_id < NUM_TX_DESCS_PER_AC * NUM_ACS) {
		DEBUG_LOG("%s-UMACTX:Reserved Token, Sending single\n",
			  dev->name);
#ifdef MULTI_CHAN_SUPPORT
		txq = &dev->tx.pkt_info[dev->curr_chanctx_idx][token_id].pkt;
#else
		txq = &dev->tx.pkt_info[token_id].pkt;
#endif
		skb_queue_tail(txq, skb);
	} else {
		/* The probability of a beacon frame not getting a reserved
		 * token is very low due since we request a beacon frame only
		 * when a reserved token is freed up. */
#ifdef MULTI_CHAN_SUPPORT
		if (uvif->chanctx->index == dev->curr_chanctx_idx) {
#endif
			mac_hdr = (struct ieee80211_hdr *)(skb->data);

			if ((queue == WLAN_AC_BCN) &&
			    (ieee80211_is_beacon(mac_hdr->frame_control))) {
				/* TODO: Need to see how to handle the beacon
				 * frame in such a case i.e. whether it is worth
				 * queuing it */
				pr_err("Did not get rsvd token for beacon\n");
			}

#ifdef MULTI_CHAN_SUPPORT
		}
#endif

		/* Queue the frame to the pending frames queue */
		skb_queue_tail(pend_pkt_q, skb);

		/* Take steps to stop the TX traffic if we have reached the
		 * queueing limit */
		if (skb_queue_len(pend_pkt_q) >= MAX_TX_QUEUE_LEN) {
			ieee80211_stop_queue(dev->hw,
					     skb->queue_mapping);
			tx->queue_stopped_bmp |= (1 << queue);
		}

		/* If we got a spare token, try sending out pending frames */
		if (token_id < NUM_TX_DESCS) {
#ifdef UNIFORM_BW_SHARING
			tx_peer_id = get_curr_peer_opp(dev, queue);
#endif

			uccp420wlan_tx_proc_pend_frms(dev,
						      queue,
#ifdef UNIFORM_BW_SHARING
						      tx_peer_id,
#endif
						      token_id);
		}
	}

	DEBUG_LOG("%s-UMACTX:Alloc buf Result *id = %d\n", dev->name, token_id);

	spin_unlock_irqrestore(&tx->lock, flags);

	/* If token is available, just return tokenid, list will be sent*/
	return token_id;
}


int uccp420wlan_tx_free_buff_req(struct mac80211_dev *dev,
				 struct umac_event_tx_done *tx_done,
				 unsigned char *queue,
				 int *vif_index_bitmap)
{
	int i = 0;
	unsigned long flags;
	unsigned int pkts_pend = 0;
	struct tx_config *tx = &dev->tx;
	struct ieee80211_hdr *mac_hdr;
	struct ieee80211_tx_info *tx_info_bcn;
	struct ieee80211_tx_info tx_info_1st_mpdu;
	struct sk_buff *skb, *tmp, *skb_first = NULL;
	struct sk_buff_head *skb_list, tx_done_list;
	int vif_index;
	unsigned int pkt = 0;
	int cnt = 0;
	int bit = 0;
	int pool_id = 0;
	unsigned int desc_id = tx_done->descriptor_id;
	unsigned int max_tx_cmds = dev->params->max_tx_cmds;
	struct umac_vif *uvif;
	struct ieee80211_vif *ivif;
	unsigned long bcn_int = 0;
	int pend_pkt_q_len = 0;
#ifdef UNIFORM_BW_SHARING
	int peer_id = 0;
#endif
#ifdef MULTI_CHAN_SUPPORT
	int chanctx_idx = 0;
#endif

	skb_queue_head_init(&tx_done_list);

	DEBUG_LOG("%s-UMACTX:Free buf Req q = %d, desc_id: %d\n",
	       dev->name,
	       tx_done->queue,
	       desc_id);

	spin_lock_irqsave(&tx->lock, flags);

	tx->outstanding_tokens[tx_done->queue]--;

#ifdef MULTI_CHAN_SUPPORT
	chanctx_idx = tx->desc_chan_map[desc_id];
#endif

	if (desc_id < (NUM_TX_DESCS_PER_AC * NUM_ACS)) {
		/* Reserved token */
		*queue = tx_done->queue;

		if (*queue != WLAN_AC_BCN) {
#ifdef UNIFORM_BW_SHARING
			peer_id = get_curr_peer_opp(dev, *queue);

			if (peer_id == -1) {
#else
			pkts_pend = skb_queue_len(&tx->pending_pkt[*queue]);

			if (!pkts_pend) {
#endif
					__clear_bit(desc_id,
						    &tx->buf_pool_bmp[0]);
#ifdef MULTI_CHAN_SUPPORT
					tx->desc_chan_map[desc_id] = -1;
#endif
				}
			} else {
				__clear_bit(desc_id,
					    &tx->buf_pool_bmp[0]);
#ifdef MULTI_CHAN_SUPPORT
				tx->desc_chan_map[desc_id] = -1;
#endif
			}
		} else {
			/* Spare token */
			for (cnt = WLAN_AC_VO; cnt >= 0; cnt--) {
#ifdef UNIFORM_BW_SHARING
				peer_id = get_curr_peer_opp(dev, cnt);

				if (peer_id != -1) {
#else
				pkts_pend =
					skb_queue_len(&tx->pending_pkt[cnt]);

				if (pkts_pend) {
#endif
						*queue = cnt;
						break;
					}
				}

				/* If beacon queue has pending and no other AC
				   has pending*/
#ifdef UNIFORM_BW_SHARING
				if (peer_id == -1) {
#else
				if (!pkts_pend) {
#endif
					bit = (desc_id %
					       TX_DESC_BUCKET_BOUND);
					pool_id = (desc_id /
						   TX_DESC_BUCKET_BOUND);

					__clear_bit(bit,
						    &tx->buf_pool_bmp[pool_id]);
#ifdef MULTI_CHAN_SUPPORT
					tx->desc_chan_map[desc_id] = -1;
#endif
				}
			}

#ifdef UNIFORM_BW_SHARING
	if (peer_id != -1)
		pkts_pend = skb_queue_len(&tx->pending_pkt[peer_id][*queue]);

	DEBUG_LOG("%s-UMACTX:%s pend_q = %d, sta_id = %d desc_id: %d pend:%d\n",
#else
	DEBUG_LOG("%s-UMACTX:%s pend_q = %d, desc_id: %d pend:%d\n",
#endif
		  dev->name,
		  __func__,
		  *queue,
#ifdef UNIFORM_BW_SHARING
		  peer_id,
#endif
		  desc_id,
		  pkts_pend);

	/* Defer Tx Done Processsing */
#ifdef MULTI_CHAN_SUPPORT
	skb_list = &dev->tx.pkt_info[chanctx_idx][desc_id].pkt;
#else
	skb_list = &dev->tx.pkt_info[desc_id].pkt;
#endif

	if (skb_queue_len(skb_list)) {
		/* Cut the list to new one, tx_pkt will be re-initialized */
		skb_queue_splice_tail_init(skb_list, &tx_done_list);
	} else {
		DEBUG_LOG("%s-UMACTX:Got Empty List: list_addr: %p\n",
			dev->name, skb_list);
	}

	if (pkts_pend > 0) {
		uccp420wlan_tx_proc_pend_frms(dev,
					      *queue,
#ifdef UNIFORM_BW_SHARING
					      peer_id,
#endif
					      desc_id);

		tx->outstanding_tokens[*queue]++;

		DEBUG_LOG("%s-UMACTX:Pending packets: %d, Total: %d\n",
		       dev->name,
		       pkts_pend,
		       skb_queue_len(skb_list));
	} else {
		DEBUG_LOG("%s-UMACTX:No Pending Packets\n", dev->name);
	}

#ifdef UNIFORM_BW_SHARING
	pend_pkt_q_len = skb_queue_len(&tx->pending_pkt[peer_id][*queue]);
#else
	pend_pkt_q_len = skb_queue_len(&tx->pending_pkt[*queue]);
#endif

	if ((*queue != WLAN_AC_BCN) &&
	    (tx->queue_stopped_bmp & (1 << *queue)) &&
	    pend_pkt_q_len < (MAX_TX_QUEUE_LEN / 2)) {
		ieee80211_wake_queue(dev->hw, tx_queue_unmap(*queue));
		tx->queue_stopped_bmp &= ~(1 << (*queue));
	}

	/* Unmap here before release lock to avoid race */
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

	/* Unlock: Give a chance for Tx to add to pending lists */
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
#ifdef MULTI_CHAN_SUPPORT
		skb_push(skb,
			 dev->tx.pkt_info[chanctx_idx][desc_id].hdr_len);
#else
		skb_push(skb,
			 dev->tx.pkt_info[tx_done->descriptor_id].hdr_len);
#endif
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
				/* We did not send beacon */
				dev->tx_last_beacon = 0;
			} else if (tx_done->frm_status[pkt] ==
				   TX_DONE_STAT_SUCCESS) {
				/* We did send beacon */
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

#ifdef MULTI_CHAN_SUPPORT
void uccp420wlan_proc_tx_discard_chsw(struct mac80211_dev *dev,
				      struct umac_event_tx_done *tx_done)
{
	struct tx_config *tx = &dev->tx;
	struct sk_buff_head *txq = NULL;
	int chanctx_idx = -1;
	int pkt = 0;
	unsigned long flags;
	int txq_len = 0;
	struct sk_buff *skb = NULL;
	struct sk_buff *tmp = NULL;
	int curr_bit = 0;
	int pool_id = 0;
	int queue = 0;
	int ret = 0;
	unsigned int desc_id = 0;

	spin_lock_irqsave(&tx->lock, flags);

	desc_id = tx_done->descriptor_id;

	/* We keep the frames which were not consumed by the FW in the
	 * tx_pkt queue. These frames will then be requeued to the FW when this
	 * channel context is scheduled again */
	chanctx_idx = tx->desc_chan_map[desc_id];

	if (chanctx_idx == -1) {
		pr_err("%s: Unexpected channel context\n", __func__);
		goto out;
	}

	txq = &tx->pkt_info[chanctx_idx][desc_id].pkt;
	txq_len = skb_queue_len(txq);

	if (!txq_len) {
		pr_err("%s: TX_DONE received for empty queue\n", __func__);
		goto out;
	}

	pkt = 0;

	skb_queue_walk_safe(txq, skb, tmp) {
		if (!skb)
			continue;

		hal_ops.unmap_tx_buf(desc_id, pkt);

		/* In the Tx path we move the .11hdr from skb to CMD_TX
		 * Hence pushing it here
		 */
		skb_push(skb,
			 tx->pkt_info[chanctx_idx][desc_id].hdr_len);

		pkt++;
	}

	if (chanctx_idx != dev->curr_chanctx_idx) {
		/* First check if there is a packet in the txq of the current
		 * chanctx that needs to be transmitted */
		txq = &tx->pkt_info[dev->curr_chanctx_idx][desc_id].pkt;
		txq_len = skb_queue_len(txq);
		queue = tx->pkt_info[dev->curr_chanctx_idx][desc_id].queue;

		if (txq_len) {
			spin_unlock_irqrestore(&tx->lock, flags);

			/* TODO: Currently sending 0 since this param is not
			   used as expected in the orig code for multiple
			   frames etc Need to set this properly when the orig
			   code logic is corrected */
			ret = __uccp420wlan_tx_frame(dev,
						     queue,
						     desc_id,
						     0);
			if (ret < 0) {
				/* TODO: Check if we need to clear the TX bitmap
				   and desc_chan_map here */
				pr_err("%s: Queueing of TX frame to FW failed\n",
				       __func__);
			} else {
				spin_lock_irqsave(&tx->lock, flags);
				tx->desc_chan_map[desc_id] =
						dev->curr_chanctx_idx;
				spin_unlock_irqrestore(&tx->lock, flags);
			}

			return;
		}
	}

	curr_bit = (desc_id % TX_DESC_BUCKET_BOUND);
	pool_id = (desc_id / TX_DESC_BUCKET_BOUND);

	/* Mark the token as available */
	__clear_bit(curr_bit, &tx->buf_pool_bmp[pool_id]);

	tx->desc_chan_map[desc_id] = -1;

	tx->outstanding_tokens[tx_done->queue]--;

	if (txq_len == 1)
		dev->stats->tx_cmd_send_count_single--;
	else
		dev->stats->tx_cmd_send_count_multi--;

out:
	spin_unlock_irqrestore(&tx->lock, flags);
}
#endif


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
	int i = 0;
#if defined(UNIFORM_BW_SHARING) || defined(MULTI_CHAN_SUPPORT)
	int j = 0;
#endif
	struct tx_config *tx = &dev->tx;

	memset(&tx->buf_pool_bmp,
	       0,
	       sizeof(long) * ((NUM_TX_DESCS/TX_DESC_BUCKET_BOUND) + 1));

	tx->queue_stopped_bmp = 0;
	tx->next_spare_token_ac = WLAN_AC_BE;

	for (i = 0; i < NUM_ACS; i++) {
#ifdef UNIFORM_BW_SHARING
		for (j = 0; j < MAX_PEND_Q_PER_AC; j++)
			skb_queue_head_init(&tx->pending_pkt[j][i]);
#else
		skb_queue_head_init(&tx->pending_pkt[i]);
#endif

		tx->outstanding_tokens[i] = 0;
	}

	for (i = 0; i < NUM_TX_DESCS; i++) {
#ifdef MULTI_CHAN_SUPPORT
		tx->desc_chan_map[i] = -1;

		for (j = 0; j < MAX_CHANCTX; j++)
			skb_queue_head_init(&tx->pkt_info[j][i].pkt);
#else
		skb_queue_head_init(&tx->pkt_info[i].pkt);
#endif
	}

#ifdef UNIFORM_BW_SHARING
	for (j = 0; j < NUM_ACS; j++)
#ifdef MULTI_CHAN_SUPPORT
		for (i = 0; i < MAX_CHANCTX; i++)
			tx->curr_peer_opp[i][j] = 0;
#else
		tx->curr_peer_opp[j] = 0;
#endif
#endif

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
	int i = 0;
#if defined(UNIFORM_BW_SHARING) || defined(MULTI_CHAN_SUPPORT)
	int j = 0;
#endif
	unsigned long flags = 0;
	struct tx_config *tx = &dev->tx;
	struct sk_buff *skb;

	ieee80211_stop_queues(dev->hw);

	wait_for_tx_complete(tx);

	spin_lock_irqsave(&tx->lock, flags);

	for (i = 0; i < NUM_TX_DESCS; i++) {
#ifdef MULTI_CHAN_SUPPORT
		for (j = 0; j < MAX_CHANCTX; j++)
			while ((skb = skb_dequeue(&tx->pkt_info[j][i].pkt)) !=
			       NULL)
				dev_kfree_skb_any(skb);
#else
		while ((skb = skb_dequeue(&tx->pkt_info[i].pkt)) != NULL)
			dev_kfree_skb_any(skb);
#endif
	}

	for (i = 0; i < NUM_ACS; i++) {
#ifdef UNIFORM_BW_SHARING
		for (j = 0; j < MAX_PEND_Q_PER_AC; j++) {
			while ((skb =
				skb_dequeue(&tx->pending_pkt[j][i])) !=
			       NULL)
				dev_kfree_skb_any(skb);
		}
#else
		while ((skb = skb_dequeue(&tx->pending_pkt[i])) != NULL)
			dev_kfree_skb_any(skb);
#endif
	}

	spin_unlock_irqrestore(&tx->lock, flags);

	DEBUG_LOG("%s-UMACTX:Deinitialization successful\n", dev->name);
}


int __uccp420wlan_tx_frame(struct mac80211_dev *dev,
			   unsigned int queue,
			   unsigned int token_id,
			   unsigned int more_frames)
{
	struct umac_event_tx_done tx_done;
	struct sk_buff_head *txq = NULL;
	int ret = 0;
	int pkt = 0;
#ifdef MULTI_CHAN_SUPPORT
	int chan_id = 0;
#endif

	ret = uccp420wlan_prog_tx(queue, more_frames, token_id);

	if (ret < 0) {
		pr_err("%s-UMACTX: Unable to send frame, dropping ..%d\n",
		       dev->name, ret);

		tx_done.descriptor_id = token_id;
		tx_done.queue = queue;

#ifdef MULTI_CHAN_SUPPORT
		chan_id = dev->curr_chanctx_idx;
		txq = &dev->tx.pkt_info[chan_id][token_id].pkt;
#else
		txq = &dev->tx.pkt_info[token_id].pkt;
#endif

		for (pkt = 0; pkt < skb_queue_len(txq); pkt++) {
			tx_done.frm_status[pkt] = TX_DONE_STAT_ERR_RETRY_LIM;
			tx_done.rate[pkt] = 0;
		}

		uccp420wlan_tx_complete(&tx_done, dev);
	}

	return ret;
}


int uccp420wlan_tx_frame(struct sk_buff *skb,
			 struct ieee80211_sta *sta,
			 struct mac80211_dev *dev,
			 bool bcast)
{
	unsigned int queue = 0;
	unsigned int token_id = 0;
	unsigned int more_frames = 0;
	int ret = 0;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
#if defined(UNIFORM_BW_SHARING) || defined(MULTI_CHAN_SUPPORT)
	struct umac_vif *uvif = NULL;
#endif
#ifdef UNIFORM_BW_SHARING
	struct umac_sta *usta = NULL;
	int peer_id = -1;
#endif

#if defined(UNIFORM_BW_SHARING) || defined(MULTI_CHAN_SUPPORT)
	uvif = (struct umac_vif *)(tx_info->control.vif->drv_priv);
#endif

#ifdef UNIFORM_BW_SHARING
	if (sta) {
		usta = (struct umac_sta *)sta->drv_priv;
		peer_id = usta->index;
	} else {
		peer_id = MAX_PEERS + uvif->vif_index;
	}
#endif

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
		/* These are high priority frames, send them in VO */
		queue = WLAN_AC_VO;
		atomic_inc(&dev->roc_params.roc_mgmt_tx_count);
	}

	DEBUG_LOG("%s-UMACTX:%s:%d Waiting for Allocation:queue: %d qmap: %d\n",
		dev->name,
		__func__, __LINE__, queue, skb->queue_mapping);

	token_id = uccp420wlan_tx_alloc_buff_req(dev,
						 queue,
#ifdef MULTI_CHAN_SUPPORT
						 uvif,
#endif
#ifdef UNIFORM_BW_SHARING
						 peer_id,
#endif
						 skb);

	/* The frame was unable to find a reserved token */
	if (token_id == NUM_TX_DESCS) {
		DEBUG_LOG("%s-UMACTX:%s:%d Token Busy Queued:\n",
			dev->name, __func__, __LINE__);
		return NETDEV_TX_OK;
	}

	ret = __uccp420wlan_tx_frame(dev,
				     queue,
				     token_id,
				     more_frames);


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
	unsigned int pkts_pending = 0;
	unsigned char queue = 0;
	struct umac_event_noa noa_event;
	int token_id = 0;
	int qlen = 0;
#ifdef MULTI_CHAN_SUPPORT
	int chanctx_idx = 0;

	chanctx_idx = dev->curr_chanctx_idx;
#endif

	token_id = tx_done->descriptor_id;

#ifdef MULTI_CHAN_SUPPORT
	qlen = skb_queue_len(&dev->tx.pkt_info[chanctx_idx][token_id].pkt);
#else
	qlen = skb_queue_len(&dev->tx.pkt_info[token_id].pkt);
#endif

	DEBUG_LOG("%s-UMACTX:TX Done Rx for desc_id: %d qlen: %d\n",
		  dev->name,
		  tx_done->descriptor_id,
		  qlen);

	update_aux_adc_voltage(dev, tx_done->pdout_voltage);

#ifdef MULTI_CHAN_SUPPORT
	if (tx_done->frm_status[0] == TX_DONE_STAT_DISCARD_CHSW) {
		uccp420wlan_proc_tx_discard_chsw(dev, tx_done);
		return;
	}
#endif
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

		ret = __uccp420wlan_tx_frame(dev,
					     queue,
					     token_id,
					     more_frames);

	} else {
		DEBUG_LOG("%s-UMACTX:No Pending Packets\n", dev->name);
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
