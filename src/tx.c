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

			UCCP_DEBUG_TX("%s-UMACTX:WARNING: ", dev->name);
			UCCP_DEBUG_TX("TX complete failed!!\n");
			UCCP_DEBUG_TX("%s-UMACTX:After ", dev->name);
			UCCP_DEBUG_TX("%ld: bitmap is: 0x%lx\n",
			       TX_COMPLETE_TIMEOUT_TICKS,
			       tx->buf_pool_bmp[0]);
			break;
		}
	}

	if (count && (count < TX_COMPLETE_TIMEOUT_TICKS)) {
		UCCP_DEBUG_TX("%s-UMACTX:TX complete after %d timer ticks\n",
			dev->name, count);
	}
}

int tx_queue_map(int queue)
{
	unsigned int ac[4] = {WLAN_AC_VO, WLAN_AC_VI, WLAN_AC_BE, WLAN_AC_BK};

	if (queue < 4)
		return ac[queue];

	return WLAN_AC_VO;
}

int tx_queue_unmap(int queue)
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

static int check_80211_aggregation(struct mac80211_dev *dev,
				struct sk_buff *skb,
			       int ac,
#ifdef MULTI_CHAN_SUPPORT
			       int off_chanctx_idx,
#endif
			       int peer_id)
{

	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *mac_hdr = NULL, *mac_hdr_first = NULL;
	struct sk_buff *skb_first;
	struct sk_buff_head *pend_pkt_q = NULL;
	struct tx_config *tx = &dev->tx;
	bool ampdu = false, is_qos = false, addr = true;

	mac_hdr = (struct ieee80211_hdr *)skb->data;
#ifdef MULTI_CHAN_SUPPORT
	pend_pkt_q = &tx->pending_pkt[off_chanctx_idx][peer_id][ac];
#else
	pend_pkt_q = &tx->pending_pkt[peer_id][ac];
#endif
	skb_first = skb_peek(pend_pkt_q);
	if (skb_first)
		mac_hdr_first = (struct ieee80211_hdr *)skb_first->data;

	ampdu = (tx_info->flags & IEEE80211_TX_CTL_AMPDU) ? true : false;
	is_qos = ieee80211_is_data_qos(mac_hdr->frame_control);

	/* RPU has a limitation, it expects A1-A2-A3 to be same
	* for all MPDU's within an AMPDU. This is a temporary
	* solution, remove it when RPU has fix for this.
	*/
	if (skb_first &&
	    ((!ether_addr_equal(mac_hdr->addr1,
		       mac_hdr_first->addr1)) ||
	    (!ether_addr_equal(mac_hdr->addr2,
		       mac_hdr_first->addr2)) ||
	    (!ether_addr_equal(mac_hdr->addr3,
		       mac_hdr_first->addr3)))) {
		addr = false;
	}

	/*stats and debug*/
	if (!is_qos) {
		UCCP_DEBUG_TX("Not Qos\n");
		dev->stats->tx_noagg_not_qos++;
	} else if (!ampdu) {
		UCCP_DEBUG_TX("Not AMPDU\n");
		dev->stats->tx_noagg_not_ampdu++;
	} else if (!addr) {
		if (skb_first) {
			UCCP_DEBUG_TX("first: A1: %pM-A2:%pM -A3%pM not same\n",
				      mac_hdr_first->addr1,
				      mac_hdr_first->addr2,
				      mac_hdr_first->addr3);
			UCCP_DEBUG_TX("curr:  A1: %pM-A2:%pM -A3%pM not same\n",
				      mac_hdr->addr1,
				      mac_hdr->addr2,
				      mac_hdr->addr3);
		}
		dev->stats->tx_noagg_not_addr++;
	}

	return (ampdu && is_qos && addr);
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
	struct umac_vif *uvif = NULL;
	int ret = 0;

	uvif = (struct umac_vif *)(tx_info->control.vif->drv_priv);

	/*Just inform ma8c0211, it will free the skb*/
	if (tx_done->frm_status[frame_idx] == TX_DONE_STAT_DISCARD) {
		ieee80211_free_txskb(dev->hw, skb);
		dev->stats->tx_dones_to_stack++;
		return;
	}

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

	if (((tx_info->flags & IEEE80211_TX_CTL_TX_OFFCHAN)
#ifdef MULTI_CHAN_SUPPORT
	     || (uvif->chanctx &&
		 (uvif->chanctx->index == dev->roc_off_chanctx_idx))
#endif
	    ) &&
	    (atomic_dec_return(&dev->roc_params.roc_mgmt_tx_count) == 0)) {
		UCCP_DEBUG_ROC("%s:%d TXDONE Frame: %d\n",
			__func__,
			__LINE__,
			atomic_read(&dev->roc_params.roc_mgmt_tx_count));
		if (dev->roc_params.roc_in_progress &&
		    dev->roc_params.roc_type == ROC_TYPE_OFFCHANNEL_TX) {
			CALL_UMAC(uccp420wlan_prog_roc, ROC_STOP, 0, 0, 0);
			UCCP_DEBUG_ROC("%s:%d", __func__, __LINE__);
			UCCP_DEBUG_ROC("all offchan pending frames cleared\n");
		}
	}

	dev->stats->tx_dones_to_stack++;

	ieee80211_tx_status(dev->hw, skb);
prog_umac_fail:
	return;
}


static int get_token(struct mac80211_dev *dev,
#ifdef MULTI_CHAN_SUPPORT
		     int curr_chanctx_idx,
#endif
		     int queue)
{
	int cnt = 0;
	int curr_bit = 0;
	int pool_id = 0;
	int token_id = NUM_TX_DESCS;
	struct tx_config *tx = &dev->tx;

	/* First search for a reserved token */
	for (cnt = 0; cnt < NUM_TX_DESCS_PER_AC; cnt++) {
		curr_bit = ((queue + (NUM_ACS * cnt)) % TX_DESC_BUCKET_BOUND);
		pool_id = ((queue + (NUM_ACS * cnt)) / TX_DESC_BUCKET_BOUND);

		if (!test_and_set_bit(curr_bit, &tx->buf_pool_bmp[pool_id])) {
			token_id = queue + (NUM_ACS * cnt);
			tx->outstanding_tokens[queue]++;
			break;
		}
	}

	/* If reserved token is not found search for a spare token
	 * (only for non beacon queues)
	 */
	if ((cnt == NUM_TX_DESCS_PER_AC) && (queue != WLAN_AC_BCN)) {
		for (token_id = NUM_TX_DESCS_PER_AC * NUM_ACS;
		     token_id < NUM_TX_DESCS;
		     token_id++) {
			curr_bit = (token_id % TX_DESC_BUCKET_BOUND);
			pool_id = (token_id / TX_DESC_BUCKET_BOUND);
			if (!test_and_set_bit(curr_bit,
					      &tx->buf_pool_bmp[pool_id])) {
				tx->outstanding_tokens[queue]++;
				break;
			}
		}
	}

	return token_id;
}

void free_token(struct mac80211_dev *dev,
		int token_id,
		int queue)
{
	struct tx_config *tx = &dev->tx;
	int bit = -1;
	int pool_id = -1;
	int test = 0;
	unsigned int old_token = tx->outstanding_tokens[queue];

	bit = (token_id % TX_DESC_BUCKET_BOUND);
	pool_id = (token_id / TX_DESC_BUCKET_BOUND);

	__clear_bit(bit, &tx->buf_pool_bmp[pool_id]);

	tx->outstanding_tokens[queue]--;

	test = tx->outstanding_tokens[queue];
	if (WARN_ON_ONCE(test < 0 || test > 4)) {
		UCCP_DEBUG_TX("%s: invalid outstanding_tokens: %d, old:%d\n",
			      __func__,
			      test,
			      old_token);
	}
}


struct curr_peer_info get_curr_peer_opp(struct mac80211_dev *dev,
#ifdef MULTI_CHAN_SUPPORT
					int curr_chanctx_idx,
#endif
					int ac)
{
	unsigned int curr_peer_opp = 0;
	unsigned int curr_vif_op_chan = UMAC_VIF_CHANCTX_TYPE_OPER;
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
	struct curr_peer_info peer_info;
	unsigned int pend_q_len;
	struct sk_buff_head *pend_q = NULL;

	tx = &dev->tx;

#ifdef MULTI_CHAN_SUPPORT
	init_peer_opp = tx->curr_peer_opp[curr_chanctx_idx][ac];
#else
	init_peer_opp = tx->curr_peer_opp[ac];
#endif
	/*TODO: Optimize this loop for BCN_Q
	 */
	for (i = 0; i < MAX_PEND_Q_PER_AC; i++) {
		curr_peer_opp = (init_peer_opp + i) % MAX_PEND_Q_PER_AC;

#ifdef MULTI_CHAN_SUPPORT
		rcu_read_lock();

		/* RoC Frame do not have a "sta" entry.
		 * so we need not handle RoC stuff here
		 */
		if (curr_peer_opp < MAX_PEERS) {
			sta = rcu_dereference(dev->peers[curr_peer_opp]);

			if (!sta) {
				rcu_read_unlock();
				continue;
			}

			usta = (struct umac_sta *)(sta->drv_priv);

			vif = rcu_dereference(dev->vifs[usta->vif_index]);

			if (!vif) {
				rcu_read_unlock();
				continue;
			}


			uvif = (struct umac_vif *)(vif->drv_priv);

			if (!uvif->chanctx && !uvif->off_chanctx) {
				rcu_read_unlock();
				continue;
			}

			if ((uvif->chanctx &&
			     (uvif->chanctx->index != curr_chanctx_idx)) ||
			    !uvif->chanctx) {
				if ((uvif->off_chanctx &&
				     (uvif->off_chanctx->index !=
				      curr_chanctx_idx)) ||
				    !uvif->off_chanctx) {
					rcu_read_unlock();
					continue;
				} else {
					curr_vif_op_chan =
						UMAC_VIF_CHANCTX_TYPE_OFF;
				}
			} else {
				if (dev->roc_params.roc_in_progress &&
				    !dev->roc_params.need_offchan)
					curr_vif_op_chan =
						UMAC_VIF_CHANCTX_TYPE_OFF;
				else
					curr_vif_op_chan =
						UMAC_VIF_CHANCTX_TYPE_OPER;
			}
		} else {
			vif_index = (curr_peer_opp - MAX_PEERS);

			vif = rcu_dereference(dev->vifs[vif_index]);

			if (!vif) {
				rcu_read_unlock();
				continue;
			}

			uvif = (struct umac_vif *)(vif->drv_priv);

			if (!uvif->chanctx && !uvif->off_chanctx) {
				rcu_read_unlock();
				continue;
			}

			/* For a beacon queue we will process the frames
			 * irrespective of the current channel context.
			 * The FW will take care of transmitting them in the
			 * appropriate channel.
			 */

			if (ac != WLAN_AC_BCN &&
			    ((uvif->chanctx &&
			      (uvif->chanctx->index != curr_chanctx_idx)) ||
			     !uvif->chanctx)) {
				if ((uvif->off_chanctx &&
				     (uvif->off_chanctx->index !=
				      curr_chanctx_idx)) ||
				    !uvif->off_chanctx) {
					rcu_read_unlock();
					continue;
				} else {
					curr_vif_op_chan =
						UMAC_VIF_CHANCTX_TYPE_OFF;
				}
			} else {
				if (dev->roc_params.roc_in_progress &&
				    !dev->roc_params.need_offchan)
					curr_vif_op_chan =
						UMAC_VIF_CHANCTX_TYPE_OFF;
				else
					curr_vif_op_chan =
						UMAC_VIF_CHANCTX_TYPE_OPER;
			}
		}

		rcu_read_unlock();
#endif
#ifdef MULTI_CHAN_SUPPORT
		pend_q = &tx->pending_pkt[curr_vif_op_chan][curr_peer_opp][ac];
#else
		pend_q = &tx->pending_pkt[curr_peer_opp][ac];
#endif
		pend_q_len = skb_queue_len(pend_q);

		if (pend_q_len) {
#ifdef MULTI_CHAN_SUPPORT
			tx->curr_peer_opp[curr_chanctx_idx][ac] =
				(curr_peer_opp + 1) % MAX_PEND_Q_PER_AC;
#else
			tx->curr_peer_opp[ac] =
				(curr_peer_opp + 1) % MAX_PEND_Q_PER_AC;
#endif
			break;
		}
	}

	if (i == MAX_PEND_Q_PER_AC) {
		peer_info.id = -1;
		peer_info.op_chan_idx = -1;
	} else {
		peer_info.id = curr_peer_opp;
		peer_info.op_chan_idx = curr_vif_op_chan;
		UCCP_DEBUG_TX("%s: Queue: %d Peer: %d op_chan: %d ",
			__func__,
			ac,
			curr_peer_opp,
			curr_vif_op_chan);
#ifdef MULTI_CHAN_SUPPORT
		UCCP_DEBUG_TX("chanctx: %d got opportunity, pending: %d\n",
			curr_chanctx_idx,
			pend_q_len);
#else
		UCCP_DEBUG_TX("Pending: %d\n",
			pend_q_len);
#endif
	}

	return peer_info;
}


#ifdef MULTI_CHAN_SUPPORT
void uccp420wlan_tx_proc_send_pend_frms_all(struct mac80211_dev *dev,
					    int ch_id)
{
	int txq_len = 0;
	int i = 0, cnt = 0;
	int queue = 0;
	int curr_bit = 0;
	int pool_id = 0;
	int ret = 0;
	int start_ac, end_ac;
	unsigned int pkts_pend = 0;
	struct tx_config *tx = NULL;
	struct sk_buff_head *txq = NULL;

	tx = &dev->tx;

	for (i = 0; i < NUM_TX_DESCS; i++) {
		spin_lock_bh(&tx->lock);

		curr_bit = (i % TX_DESC_BUCKET_BOUND);
		pool_id = (i / TX_DESC_BUCKET_BOUND);

		if (test_and_set_bit(curr_bit, &tx->buf_pool_bmp[pool_id])) {
			spin_unlock_bh(&tx->lock);
			continue;
		}

		txq = &tx->pkt_info[ch_id][i].pkt;
		txq_len = skb_queue_len(txq);

		/* Not valid when txq len is 0 */
		queue = tx->pkt_info[ch_id][i].queue;

		if (!txq_len) {
			/* Reserved token */
			if (i < (NUM_TX_DESCS_PER_AC * NUM_ACS)) {
				queue = (i % NUM_ACS);
				start_ac = end_ac = queue;
			} else {
				/* Spare token:
				 * Loop through all AC's
				 */
				start_ac = WLAN_AC_VO;
				end_ac = WLAN_AC_BK;
			}

			for (cnt = start_ac; cnt >= end_ac; cnt--) {
				pkts_pend = uccp420wlan_tx_proc_pend_frms(dev,
									  cnt,
									  ch_id,
									  i);
				if (pkts_pend) {
					queue = cnt;
					break;
				}
			}

			if (pkts_pend == 0) {
				__clear_bit(curr_bit,
					    &tx->buf_pool_bmp[pool_id]);
				spin_unlock_bh(&tx->lock);
				continue;
			}
		}

		tx->outstanding_tokens[queue]++;
		spin_unlock_bh(&tx->lock);

		ret = __uccp420wlan_tx_frame(dev,
					     queue,
					     i,
					     ch_id,
					     0,
					     0); /* TODO: Currently sending 0
						  * since this param is not used
						  * as expected in the orig
						  * code for multiple frames etc
						  * Need to set this
						  * properly when the orig code
						  * logic is corrected
						  */
		if (ret < 0) {
			pr_err("%s: Queueing of TX frame to FW failed\n",
			       __func__);
		}
	}
}
#endif


int uccp420wlan_tx_proc_pend_frms(struct mac80211_dev *dev,
				  int ac,
#ifdef MULTI_CHAN_SUPPORT
				  int curr_chanctx_idx,
#endif
				  int token_id)
{
	struct tx_config *tx = &dev->tx;
	unsigned long ampdu_len = 0;
	struct sk_buff *loop_skb = NULL;
	struct sk_buff *tmp = NULL;
	struct ieee80211_hdr *mac_hdr = NULL;
	struct ieee80211_tx_info *tx_info = NULL;
	struct umac_vif *uvif = NULL;
	struct ieee80211_vif *ivif = NULL;
	unsigned char *data = NULL;
	unsigned int max_tx_cmds = dev->params->max_tx_cmds;
	struct sk_buff_head *txq = NULL;
	struct sk_buff_head *pend_pkt_q = NULL;
	unsigned int total_pending_processed = 0;
	int pend_pkt_q_len = 0;
	struct curr_peer_info peer_info;
	int loop_cnt = 0;
	struct tx_pkt_info *pkt_info = NULL;

	peer_info = get_curr_peer_opp(dev,
#ifdef MULTI_CHAN_SUPPORT
				       curr_chanctx_idx,
#endif
				       ac);

	/* No pending frames for any peer in that AC.
	 */
	if (peer_info.id == -1)
		return 0;

#ifdef MULTI_CHAN_SUPPORT
	pend_pkt_q = &tx->pending_pkt[peer_info.op_chan_idx][peer_info.id][ac];
#else
	pend_pkt_q = &tx->pending_pkt[peer_info.id][ac];
#endif

#ifdef MULTI_CHAN_SUPPORT
	pkt_info = &dev->tx.pkt_info[curr_chanctx_idx][token_id];
#else
	pkt_info = &dev->tx.pkt_info[token_id];
#endif
	txq = &pkt_info->pkt;


	/* Aggregate Only MPDU's with same RA, same Rate,
	 * same Rate flags, same Tx Info flags
	 */
	skb_queue_walk_safe(pend_pkt_q,
			    loop_skb,
			    tmp) {
		data = loop_skb->data;
		mac_hdr = (struct ieee80211_hdr *)data;

		tx_info = IEEE80211_SKB_CB(loop_skb);

		ivif = tx_info->control.vif;
		uvif = (struct umac_vif *)(ivif->drv_priv);

		ampdu_len += loop_skb->len;

		/* Temp Checks for Aggregation: Will be removed later*/
		if (vht_support && !loop_cnt)
			if ((tx_info->control.rates[0].flags &
			     IEEE80211_TX_RC_MCS) &&
			    max_tx_cmds > MAX_SUBFRAMES_IN_AMPDU_HT)
				max_tx_cmds = MAX_SUBFRAMES_IN_AMPDU_HT;
		if (!check_80211_aggregation(dev,
					     loop_skb,
					     ac,
					     peer_info.op_chan_idx,
					     peer_info.id) ||
		    (skb_queue_len(txq) >= max_tx_cmds)) {
			break;
		}
		loop_cnt++;
		__skb_unlink(loop_skb, pend_pkt_q);
		skb_queue_tail(txq, loop_skb);
	}

	/* If our criterion rejects all pending frames, or
	 * pend_q is empty, send only 1
	 */
	if (!skb_queue_len(txq))
		skb_queue_tail(txq, skb_dequeue(pend_pkt_q));

	total_pending_processed = skb_queue_len(txq);

	pend_pkt_q_len = skb_queue_len(pend_pkt_q);
	if ((ac != WLAN_AC_BCN) &&
	    (tx->queue_stopped_bmp & (1 << ac)) &&
	    pend_pkt_q_len < (MAX_TX_QUEUE_LEN / 2)) {
		ieee80211_wake_queue(dev->hw, tx_queue_unmap(ac));
		tx->queue_stopped_bmp &= ~(1 << (ac));
	}

	pkt_info->peer_id = peer_info.id;
	UCCP_DEBUG_TX("%s-UMACTX: token_id: %d ",
				dev->name,
				token_id);
	UCCP_DEBUG_TX("total_pending_packets_process: %d\n",
		skb_queue_len(txq));

	return total_pending_processed;
}


int uccp420wlan_tx_alloc_token(struct mac80211_dev *dev,
			       int ac,
#ifdef MULTI_CHAN_SUPPORT
			       int off_chanctx_idx,
			       int curr_chanctx_idx,
#endif
			       int peer_id,
			       struct sk_buff *skb)
{
	int token_id = NUM_TX_DESCS;
	struct tx_config *tx = &dev->tx;
	struct sk_buff_head *pend_pkt_q = NULL;
	unsigned int pkts_pend = 0;
	struct ieee80211_tx_info *tx_info;

	spin_lock_bh(&tx->lock);
#ifdef MULTI_CHAN_SUPPORT
	pend_pkt_q = &tx->pending_pkt[off_chanctx_idx][peer_id][ac];

#else
	pend_pkt_q = &tx->pending_pkt[peer_id][ac];
#endif
#ifdef MULTI_CHAN_SUPPORT
	UCCP_DEBUG_TX("%s-UMACTX:Alloc Req q = %d off_chan: %d out_tok:%d\n",
		      dev->name,
		      ac,
		      off_chanctx_idx, tx->outstanding_tokens[ac]);
#else
	UCCP_DEBUG_TX("%s-UMACTX:Alloc buf Req q = %d\n",
		      dev->name,
		      ac);
#endif
	UCCP_DEBUG_TX("peerid: %d,\n", peer_id);

	/* Queue the frame to the pending frames queue */
	skb_queue_tail(pend_pkt_q, skb);

	tx_info = IEEE80211_SKB_CB(skb);

	if (tx->outstanding_tokens[ac] >= NUM_TX_DESCS_PER_AC) {
		bool agg_status = false;

		agg_status = check_80211_aggregation(dev,
						     skb,
						     ac,
						     off_chanctx_idx,
						     peer_id);

		if (agg_status || !dev->params->enable_early_agg_checks) {
			int max_cmds = dev->params->max_tx_cmds;

			/* encourage aggregation to the max size
			 * supported (dev->params->max_tx_cmds)
			 */
			if (skb_queue_len(pend_pkt_q) < max_cmds) {
				UCCP_DEBUG_TX("pend_q not full out_tok:%d\n",
					      tx->outstanding_tokens[ac]);
				goto out;
			 } else {
				UCCP_DEBUG_TX("pend_q full out_tok:%d\n",
					      tx->outstanding_tokens[ac]);
			}
		}
	}

	/* Take steps to stop the TX traffic if we have reached
	 * the queueing limit.
	 * We dont this for the ROC queue to avoid the case where we are in the
	 * OFF channel but there is lot of traffic for the operating channel on
	 * the shared ROC queue (which is VO right now), since this would block
	 * ROC traffic too.
	 */
	if (skb_queue_len(pend_pkt_q) >= MAX_TX_QUEUE_LEN) {
		if ((!dev->roc_params.roc_in_progress) ||
		    (dev->roc_params.roc_in_progress &&
		     (ac != UMAC_ROC_AC))) {
			ieee80211_stop_queue(dev->hw,
					     skb->queue_mapping);
			tx->queue_stopped_bmp |= (1 << ac);
		}
	}

	token_id = get_token(dev,
#ifdef MULTI_CHAN_SUPPORT
			     curr_chanctx_idx,
#endif
			     ac);

	UCCP_DEBUG_TX("%s-UMACTX:Alloc buf Result *id= %d q = %d out_tok: %d",
					dev->name,
					token_id,
					ac, tx->outstanding_tokens[ac]);
	UCCP_DEBUG_TX(", peerid: %d,\n", peer_id);

	if (token_id == NUM_TX_DESCS)
		goto out;

	pkts_pend = uccp420wlan_tx_proc_pend_frms(dev,
						  ac,
#ifdef MULTI_CHAN_SUPPORT
						  curr_chanctx_idx,
#endif
						  token_id);

	/* We have just added a frame to pending_q but channel context is
	 * mismatch.
	 */

	if (!pkts_pend) {
		free_token(dev, token_id, ac);
		token_id = NUM_TX_DESCS;
	}

out:
	spin_unlock_bh(&tx->lock);

	UCCP_DEBUG_TX("%s-UMACTX:Alloc buf Result *id= %d out_tok:%d\n",
					dev->name,
					token_id, tx->outstanding_tokens[ac]);
	/* If token is available, just return tokenid, list will be sent*/
	return token_id;
}


#ifdef MULTI_CHAN_SUPPORT
int get_band_chanctx(struct mac80211_dev *dev, struct umac_vif *uvif)
{
	struct ieee80211_chanctx_conf *chanctx = NULL;
	int index = 0;
	int band = 0;

	rcu_read_lock();
	index = uvif->chanctx->index;
	chanctx = rcu_dereference(dev->chanctx[index]);
	band = (chanctx->def.chan)->band;
	rcu_read_unlock();

	return band;
}
#endif

int uccp420wlan_tx_free_buff_req(struct mac80211_dev *dev,
				 struct umac_event_tx_done *tx_done,
				 unsigned char *ac,
#ifdef MULTI_CHAN_SUPPORT
				 int curr_chanctx_idx,
#endif
				 int *vif_index_bitmap)
{
	int i = 0;
	unsigned int pkts_pend = 0;
	struct tx_config *tx = &dev->tx;
	struct ieee80211_hdr *mac_hdr;
	struct ieee80211_tx_info *tx_info_bcn;
	struct ieee80211_tx_info tx_info_1st_mpdu;
	struct sk_buff *skb, *tmp, *skb_first = NULL;
	struct sk_buff_head *skb_list, tx_done_list;
	int vif_index = -1;
	unsigned int pkt = 0;
	int cnt = 0;
	unsigned int desc_id = tx_done->descriptor_id;
	struct umac_vif *uvif = NULL;
	struct ieee80211_vif *ivif = NULL;
	unsigned long bcn_int = 0;
#ifdef MULTI_CHAN_SUPPORT
	int chanctx_idx = 0;
	struct tx_pkt_info *pkt_info = NULL;
#endif
	int start_ac, end_ac;

	skb_queue_head_init(&tx_done_list);

	spin_lock_bh(&tx->lock);

#ifdef MULTI_CHAN_SUPPORT
	chanctx_idx = tx->desc_chan_map[desc_id];
	if (chanctx_idx == -1) {
		spin_unlock_bh(&tx->lock);
		if (net_ratelimit())
			pr_err("%s: Unexpected channel context:tok:%d q:%d\n",
			       __func__,
			       desc_id,
			       tx_done->queue);
		goto out;
	}
	pkt_info = &dev->tx.pkt_info[chanctx_idx][desc_id];
#endif
	UCCP_DEBUG_TX("%s-UMACTX:Free buf Req q = %d",
				dev->name,
				tx_done->queue);
#ifdef MULTI_CHAN_SUPPORT
	UCCP_DEBUG_TX(", desc_id: %d chanctx: %d out_tok: %d\n",
				desc_id,
				chanctx_idx,
				dev->tx.outstanding_tokens[tx_done->queue]);
#else
	UCCP_DEBUG_TX(", desc_id: %d out_tok: %d\n",
				desc_id,
				dev->tx.outstanding_tokens[tx_done->queue]);
#endif


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
		UCCP_DEBUG_TX("%s-UMACTX:Got Empty List: list_addr: %p\n",
						dev->name,
						skb_list);
	}

	/* Reserved token */
	if (desc_id < (NUM_TX_DESCS_PER_AC * NUM_ACS)) {
		start_ac = end_ac = tx_done->queue;
	} else {
		/* Spare token:
		 * Loop through all AC's
		 */
		start_ac = WLAN_AC_VO;
		end_ac = WLAN_AC_BK;
	}
	for (cnt = start_ac; cnt >= end_ac; cnt--) {
		pkts_pend = uccp420wlan_tx_proc_pend_frms(dev,
					      cnt,
#ifdef MULTI_CHAN_SUPPORT
					      curr_chanctx_idx,
#endif
					      desc_id);

		if (pkts_pend) {
			*ac = cnt;
			/* Spare Token Case*/
			if (tx_done->queue != *ac) {
				/*Adjust the counters*/
				tx->outstanding_tokens[tx_done->queue]--;
				tx->outstanding_tokens[*ac]++;
			}
			break;
		}
	}

	/* Unmap here before release lock to avoid race */
	if (skb_queue_len(&tx_done_list)) {
		skb_queue_walk_safe(&tx_done_list, skb, tmp) {
			hal_ops.unmap_tx_buf(tx_done->descriptor_id, pkt);
			UCCP_DEBUG_TX("%s-UMACTX:TXDONE: ID=%d",
				dev->name,
				tx_done->descriptor_id);
			UCCP_DEBUG_TX("Stat=%d (%d, %d)\n",
				tx_done->frm_status[pkt],
				tx_done->rate[pkt],
				tx_done->retries_num[pkt]);

			pkt++;
		}
	}

	if (!pkts_pend) {
		/* Mark the token as available */
		free_token(dev, desc_id, tx_done->queue);
#ifdef MULTI_CHAN_SUPPORT
		dev->tx.desc_chan_map[desc_id] = -1;
#endif
	}

	/* Unlock: Give a chance for Tx to add to pending lists */
	spin_unlock_bh(&tx->lock);

	/* Protection from mac80211 _ops especially stop */
	if (dev->state != STARTED)
		goto out;

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
			struct ieee80211_bss_conf *bss_conf;
			bool bcn_status;

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

			bss_conf = &uvif->vif->bss_conf;
			bcn_status = bss_conf->enable_beacon;
			bcn_int = bss_conf->beacon_int - 10;
			bcn_int = msecs_to_jiffies(bcn_int);

			for (i = 0; i < MAX_VIFS; i++) {
				if (dev->active_vifs & (1 << i)) {
					if ((dev->vifs[i] == ivif) &&
					    (bcn_status == true)) {
						mod_timer(&uvif->bcn_timer,
							  jiffies +
							  bcn_int);
					}
				}
			}

			/* Beacon Time Stamp */
			if (tx_done->frm_status[pkt] == TX_DONE_STAT_SUCCESS) {
				unsigned int ts2;
				unsigned int ldelta = 0;
				int ets_band;
				int bts_vif = uvif->vif_index;
				struct wifi_sync sync =
						dev->params->sync[bts_vif];
#ifdef MULTI_CHAN_SUPPORT
				ets_band = get_band_chanctx(dev, uvif);
#endif
				spin_lock(&tsf_lock);
				sync.status = 1;
				ether_addr_copy(sync.bssid,
					ivif->bss_conf.bssid);
				memcpy(sync.ts1, tx_done->reserved, 8);
				memcpy(&sync.ts2, (tx_done->reserved + 8), 4);
				ts2 = sync.ts2;
				sync.atu = 0;

				if (ets_band == IEEE80211_BAND_2GHZ)
					ldelta = BTS_AP_24GHZ_ETS;
				else if (ets_band == IEEE80211_BAND_5GHZ)
					ldelta = BTS_AP_5GHZ_ETS;

				if (frc_to_atu) {
					frc_to_atu(ts2, &sync.atu, 0);
					sync.atu += ldelta * 1000;
				}
				spin_unlock(&tsf_lock);
			}
			dev_kfree_skb_any(skb);
		}

		pkt++;
	}
out:
	return pkts_pend;
}


#ifdef MULTI_CHAN_SUPPORT
void uccp420wlan_proc_ch_sw_event(struct umac_event_ch_switch *ch_sw_info,
				  void *context)
{
	struct mac80211_dev *dev = NULL;
	int chan = 0;
	int curr_freq = 0;
	int chan_id = 0;
	struct ieee80211_chanctx_conf *curr_chanctx = NULL;
	int i = 0;

	if (!ch_sw_info || !context) {
		pr_err("%s: Invalid Parameters:\n", __func__);
		return;
	}

	dev = (struct mac80211_dev *)context;
	chan = ch_sw_info->chan;

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
		pr_err("%s: Invalid Channel Context: chan: %d\n",
		       __func__,
		       chan);
		return;
	}

	/* Switch to the new channel context */
	spin_lock(&dev->chanctx_lock);
	dev->curr_chanctx_idx = chan_id;
	spin_unlock(&dev->chanctx_lock);

	/* We now try to xmit any frames whose xmission got cancelled due to a
	 * previous channel switch
	 */
	uccp420wlan_tx_proc_send_pend_frms_all(dev, chan_id);
}


unsigned int uccp420wlan_proc_tx_dscrd_chsw(struct mac80211_dev *dev,
					    int curr_chanctx_idx,
					    struct umac_event_tx_done *tx_done)
{
	struct tx_config *tx = &dev->tx;
	struct sk_buff_head *txq = NULL, tx_done_list;
	int chanctx_idx = -1;
	int pkt = 0;
	int txq_len = 0;
	struct sk_buff *skb = NULL;
	struct sk_buff *skb_first = NULL;
	struct sk_buff *tmp = NULL;
	int queue = 0;
	int ret = 0, cnt = 0;
	unsigned int desc_id = 0;
	unsigned int *curr_retries = NULL;
	unsigned int max_retries = 0;
	struct ieee80211_tx_info tx_info_1st_mpdu;
	struct ieee80211_hdr *mac_hdr = NULL;
	bool retries_exceeded = false;
	unsigned int *rate = NULL;
	unsigned int *retries = NULL;
	int start_ac, end_ac;
	unsigned int pkts_pend = 0;

	skb_queue_head_init(&tx_done_list);

	spin_lock_bh(&tx->lock);

	desc_id = tx_done->descriptor_id;

	/* We keep the frames which were not consumed by the FW in the
	 * tx_pkt queue. These frames will then be requeued to the FW when this
	 * channel context is scheduled again
	 */
	chanctx_idx = tx->desc_chan_map[desc_id];

	if ((chanctx_idx == -1) ||
	    (chanctx_idx > (MAX_CHANCTX + MAX_OFF_CHANCTX))) {
		pr_err("%s: Unexpected channel context: %d\n",
		       __func__,
		       chanctx_idx);
		goto out;
	}

	txq = &tx->pkt_info[chanctx_idx][desc_id].pkt;
	txq_len = skb_queue_len(txq);

	if (!txq_len) {
		pr_err("%s: TX_DONE received for empty queue: chan: %d desc_id: %d\n",
		       __func__,
		       chanctx_idx,
		       desc_id);
		goto out;
	}

	UCCP_DEBUG_TX("%s: %d retries: %d rate: %d\n",
			__func__,
			__LINE__,
			tx_done->retries_num[0],
			tx_done->rate[0]);
	pkt = 0;

	skb_first = skb_peek(txq);

	if (!skb_first) {
		pr_err("%s: Empty txq: chan: %d desc_id: %d\n",
		       __func__,
		       chanctx_idx,
		       desc_id);
		goto out;
	}

	curr_retries = &tx->pkt_info[chanctx_idx][desc_id].curr_retries;
	max_retries = tx->pkt_info[chanctx_idx][desc_id].max_retries;
	retries = tx->pkt_info[chanctx_idx][desc_id].retries;
	rate = tx->pkt_info[chanctx_idx][desc_id].rate;
	tx->pkt_info[chanctx_idx][desc_id].adjusted_rates = true;

	if ((tx_done->retries_num[0] + *curr_retries) > max_retries)
		retries_exceeded = true;
	else
		*curr_retries += tx_done->retries_num[0];

	memcpy(&tx_info_1st_mpdu,
	       (struct ieee80211_tx_info *)IEEE80211_SKB_CB(skb_first),
	       sizeof(struct ieee80211_tx_info));

	skb_queue_walk_safe(txq, skb, tmp) {
		if (!skb)
			continue;

		hal_ops.unmap_tx_buf(desc_id, pkt);

		/* In the Tx path we move the .11hdr from skb to CMD_TX
		 * Hence pushing it here
		 */
		skb_push(skb,
			 tx->pkt_info[chanctx_idx][desc_id].hdr_len);

		mac_hdr = (struct ieee80211_hdr *)skb->data;

		if (retries_exceeded) {
			__skb_unlink(skb, txq);

			if (!skb)
				continue;
			skb_queue_tail(&tx_done_list, skb);

			UCCP_DEBUG_TX("%s: %d ", __func__, __LINE__);
			UCCP_DEBUG_TX("Freeing the skb MAX retries reached.\n");
		} else {
			UCCP_DEBUG_TX("%s: %d ", __func__, __LINE__);
			UCCP_DEBUG_TX("Re-programming the skb when ");
			UCCP_DEBUG_TX("CTX is right with retry bit set.\n");
		mac_hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_RETRY);
		}
		pkt++;
	}

	/* First check if there is a packet in the txq of the current
	 * chanctx that needs to be transmitted
	 */
	txq = &tx->pkt_info[curr_chanctx_idx][desc_id].pkt;
	txq_len = skb_queue_len(txq);
	queue = tx->pkt_info[curr_chanctx_idx][desc_id].queue;
	pkts_pend = txq_len;

	if (txq_len) {
		spin_unlock_bh(&tx->lock);

		/* TODO: Currently sending 0 since this param is not
		 * used as expected in the orig code for multiple
		 * frames etc Need to set this properly when the orig
		 * code logic is corrected
		 */
		ret = __uccp420wlan_tx_frame(dev,
					     queue,
					     desc_id,
					     curr_chanctx_idx,
					     0,
					     1);
		if (ret < 0) {
			/* TODO: Check if we need to clear the TX bitmap
			 * and desc_chan_map here
			 */
			pr_err("%s: Queueing of TX frame to FW failed\n",
			       __func__);
		}

		/* This is needed to avoid freeing up the token
		 */
		pkts_pend = 1;

		goto tx_done;
	} else {
		/* Check pending queue */
		/* Reserved token */
		if (desc_id < (NUM_TX_DESCS_PER_AC * NUM_ACS)) {
			queue = (desc_id % NUM_ACS);
			start_ac = end_ac = queue;
		} else {
			/* Spare token:
			 * Loop through all AC's
			 */
			start_ac = WLAN_AC_VO;
			end_ac = WLAN_AC_BK;
		}

		for (cnt = start_ac; cnt >= end_ac; cnt--) {
			pkts_pend = uccp420wlan_tx_proc_pend_frms(dev,
						      cnt,
						      curr_chanctx_idx,
						      desc_id);
			if (pkts_pend) {
				queue = cnt;
				if (tx_done->queue != queue) {
					unsigned int txd_q = tx_done->queue;
					/*Adjust the counters*/
					tx->outstanding_tokens[txd_q]--;
					tx->outstanding_tokens[queue]++;
				}
				break;
			}
		}

		spin_unlock_bh(&tx->lock);

		if (pkts_pend > 0) {
			/* TODO: Currently sending 0 since this param is not
			 * used as expected in the orig code for multiple
			 * frames etc. Need to set this properly when the orig
			 * code logic is corrected
			 */
			ret = __uccp420wlan_tx_frame(dev,
						     queue,
						     desc_id,
						     curr_chanctx_idx,
						     0,
						     0);

			if (ret < 0) {
				pr_err("%s: Queueing of TX frame to FW failed\n",
				       __func__);
			}
		}
		goto tx_done;
	}

tx_done:
	skb_queue_walk_safe(&tx_done_list, skb, tmp) {
			tx_status(skb,
				  tx_done,
				  pkt,
				  dev,
				  tx_info_1st_mpdu);
	}

	spin_lock_bh(&tx->lock);

	if (!pkts_pend) {
		/* Mark the token as available */
		free_token(dev, desc_id, tx_done->queue);
		dev->tx.desc_chan_map[desc_id] = -1;
	}
out:
	spin_unlock_bh(&tx->lock);

	return pkts_pend;
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
	int j = 0;
#ifdef MULTI_CHAN_SUPPORT
	int k = 0;
#endif
	struct tx_config *tx = &dev->tx;

	memset(&tx->buf_pool_bmp,
	       0,
	       sizeof(long) * ((NUM_TX_DESCS/TX_DESC_BUCKET_BOUND) + 1));

	tx->queue_stopped_bmp = 0;
	tx->next_spare_token_ac = WLAN_AC_BE;

	for (i = 0; i < NUM_ACS; i++) {
		for (j = 0; j < MAX_PEND_Q_PER_AC; j++) {
#ifdef MULTI_CHAN_SUPPORT
			for (k = 0; k < MAX_UMAC_VIF_CHANCTX_TYPES; k++)
				skb_queue_head_init(&tx->pending_pkt[k][j][i]);
#else
				skb_queue_head_init(&tx->pending_pkt[j][i]);
#endif
		}

		tx->outstanding_tokens[i] = 0;
	}

	for (i = 0; i < NUM_TX_DESCS; i++) {
#ifdef MULTI_CHAN_SUPPORT
		tx->desc_chan_map[i] = -1;

		for (j = 0; j < MAX_CHANCTX + MAX_OFF_CHANCTX ; j++)
			skb_queue_head_init(&tx->pkt_info[j][i].pkt);
#else
		skb_queue_head_init(&tx->pkt_info[i].pkt);
#endif
	}

	for (j = 0; j < NUM_ACS; j++)
#ifdef MULTI_CHAN_SUPPORT
		for (i = 0; i < MAX_CHANCTX; i++)
			tx->curr_peer_opp[i][j] = 0;
#else
		tx->curr_peer_opp[j] = 0;
#endif

#ifdef PERF_PROFILING
	init_timer(&tx->persec_timer);
	tx->persec_timer.data = (unsigned long)dev;
	tx->persec_timer.function = print_persec_stats;
	mod_timer(&tx->persec_timer, jiffies + msecs_to_jiffies(1000));
#endif
#ifdef MULTI_CHAN_SUPPORT
	dev->curr_chanctx_idx = -1;
#endif
	spin_lock_init(&tx->lock);
	ieee80211_wake_queues(dev->hw);

	UCCP_DEBUG_TX("%s-UMACTX: initialization successful\n",
			TX_TO_MACDEV(tx)->name);
}


void uccp420wlan_tx_deinit(struct mac80211_dev *dev)
{
	int i = 0;
	int j = 0;
#ifdef MULTI_CHAN_SUPPORT
	int k = 0;
#endif
	struct tx_config *tx = &dev->tx;
	struct sk_buff *skb = NULL;
#ifdef MULTI_CHAN_SUPPORT
	unsigned int qlen = 0;
#endif
	struct sk_buff_head *pend_q = NULL;

	ieee80211_stop_queues(dev->hw);

	wait_for_tx_complete(tx);

	spin_lock_bh(&tx->lock);

	for (i = 0; i < NUM_TX_DESCS; i++) {
#ifdef MULTI_CHAN_SUPPORT
		for (j = 0; j < MAX_CHANCTX + MAX_OFF_CHANCTX; j++) {
			qlen = skb_queue_len(&tx->pkt_info[j][i].pkt);

			if (qlen) {
				while ((skb =
					skb_dequeue(&tx->pkt_info[j][i].pkt)) !=
				       NULL) {
					dev_kfree_skb_any(skb);
				}
			}
		}
#else
		while ((skb = skb_dequeue(&tx->pkt_info[i].pkt)) != NULL)
			dev_kfree_skb_any(skb);
#endif
	}

	for (i = 0; i < NUM_ACS; i++) {
		for (j = 0; j < MAX_PEND_Q_PER_AC; j++) {
#ifdef MULTI_CHAN_SUPPORT
			for (k = 0; k < MAX_UMAC_VIF_CHANCTX_TYPES; k++)
				pend_q = &tx->pending_pkt[k][j][i];
#else
			pend_q = &tx->pending_pkt[j][i];
#endif

			while ((skb = skb_dequeue(pend_q)) != NULL)
				dev_kfree_skb_any(skb);
		}
	}

	spin_unlock_bh(&tx->lock);

	UCCP_DEBUG_TX("%s-UMACTX: deinitialization successful\n",
			TX_TO_MACDEV(tx)->name);
}


int __uccp420wlan_tx_frame(struct mac80211_dev *dev,
			   unsigned int queue,
			   unsigned int token_id,
#ifdef MULTI_CHAN_SUPPORT
			   int curr_chanctx_idx,
#endif
			   unsigned int more_frames,
			   bool retry)
{
	struct umac_event_tx_done tx_done;
	struct sk_buff_head *txq = NULL;
	int ret = 0;
	int pkt = 0;

	ret = uccp420wlan_prog_tx(queue,
				  more_frames,
#ifdef MULTI_CHAN_SUPPORT
				  curr_chanctx_idx,
#endif
				  token_id,
				  retry);

	if (ret < 0) {
		pr_err("%s-UMACTX: Unable to send frame, dropping ..%d\n",
		       dev->name, ret);

		tx_done.descriptor_id = token_id;
		tx_done.queue = queue;
#ifdef MULTI_CHAN_SUPPORT
		dev->tx.desc_chan_map[token_id] = curr_chanctx_idx;
#endif

#ifdef MULTI_CHAN_SUPPORT
		txq = &dev->tx.pkt_info[curr_chanctx_idx][token_id].pkt;
#else
		txq = &dev->tx.pkt_info[token_id].pkt;
#endif

		for (pkt = 0; pkt < skb_queue_len(txq); pkt++) {
			tx_done.frm_status[pkt] = TX_DONE_STAT_ERR_RETRY_LIM;
			tx_done.rate[pkt] = 0;
		}

		uccp420wlan_tx_complete(&tx_done,
#ifdef MULTI_CHAN_SUPPORT
					curr_chanctx_idx,
#endif
					dev);
	}

	return ret;
}


int uccp420wlan_tx_frame(struct sk_buff *skb,
			 struct ieee80211_sta *sta,
			 struct mac80211_dev *dev,
#ifdef MULTI_CHAN_SUPPORT
			 int curr_chanctx_idx,
#endif
			 bool bcast)
{
	unsigned int queue = 0;
	unsigned int token_id = 0;
	unsigned int more_frames = 0;
	int ret = 0;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *mac_hdr = NULL;
	struct umac_vif *uvif = NULL;
	struct umac_sta *usta = NULL;
	int peer_id = -1;
#ifdef MULTI_CHAN_SUPPORT
	int off_chanctx_idx;
#endif

	uvif = (struct umac_vif *)(tx_info->control.vif->drv_priv);
	mac_hdr = (struct ieee80211_hdr *)(skb->data);

	if (sta) {
		usta = (struct umac_sta *)sta->drv_priv;
		peer_id = usta->index;
	} else {
		peer_id = MAX_PEERS + uvif->vif_index;
	}

	if (bcast == false) {
		queue = tx_queue_map(skb->queue_mapping);
		more_frames = 0;
	} else {
		queue = WLAN_AC_BCN;
		/* Hack: skb->priority is used to indicate more frames */
		more_frames = skb->priority;
	}

	if (!ieee80211_is_beacon(mac_hdr->frame_control))
		dev->stats->tx_cmds_from_stack++;

	if (dev->params->production_test == 1)
		tx_info->flags |= IEEE80211_TX_CTL_AMPDU;

#ifdef MULTI_CHAN_SUPPORT
	if ((tx_info->flags & IEEE80211_TX_CTL_TX_OFFCHAN) ||
	    (uvif->chanctx &&
	    uvif->chanctx->index == dev->roc_off_chanctx_idx))  {
		atomic_inc(&dev->roc_params.roc_mgmt_tx_count);
		off_chanctx_idx = UMAC_VIF_CHANCTX_TYPE_OFF;
		UCCP_DEBUG_ROC("%s:%d Sending OFFCHAN Frame: %d\n",
			__func__, __LINE__,
			atomic_read(&dev->roc_params.roc_mgmt_tx_count));
	} else {
		off_chanctx_idx = UMAC_VIF_CHANCTX_TYPE_OPER;
	}
#endif


	UCCP_DEBUG_TX("%s-UMACTX:%s:%d ",
			dev->name,
			 __func__,
			 __LINE__);
	UCCP_DEBUG_TX("Wait Alloc:queue: %d qmap: %d is_bcn: %d bcast:%d\n",
			queue,
			skb->queue_mapping,
			ieee80211_is_beacon(mac_hdr->frame_control),
			is_multicast_ether_addr(mac_hdr->addr1) ? true : false);

	token_id = uccp420wlan_tx_alloc_token(dev,
						 queue,
#ifdef MULTI_CHAN_SUPPORT
						 off_chanctx_idx,
						 curr_chanctx_idx,
#endif
						 peer_id,
						 skb);

	/* The frame was unable to find a reserved token */
	if (token_id == NUM_TX_DESCS) {
		UCCP_DEBUG_TX("%s-UMACTX:%s:%d Token Busy Queued:\n",
			dev->name, __func__, __LINE__);
		return NETDEV_TX_OK;
	}

	ret = __uccp420wlan_tx_frame(dev,
				     queue,
				     token_id,
#ifdef MULTI_CHAN_SUPPORT
				     curr_chanctx_idx,
#endif
				     more_frames,
				     0);


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
#ifdef MULTI_CHAN_SUPPORT
			     int curr_chanctx_idx,
#endif
			     void *context)
{
	struct mac80211_dev *dev = (struct mac80211_dev *)context;
	unsigned int more_frames = 0;
	int vif_index = 0, vif_index_bitmap = 0, ret = 0;
	unsigned int pkts_pending = 0;
	unsigned char queue = 0;
	struct umac_event_noa noa_event;
	int token_id = 0;
	int qlen = 0;

	token_id = tx_done->descriptor_id;

#ifdef MULTI_CHAN_SUPPORT
	qlen = skb_queue_len(&dev->tx.pkt_info[curr_chanctx_idx][token_id].pkt);
#else
	qlen = skb_queue_len(&dev->tx.pkt_info[token_id].pkt);
#endif

	UCCP_DEBUG_TX("%s-UMACTX:TX Done Rx for desc_id: %d",
			  dev->name,
			  tx_done->descriptor_id);
#ifdef MULTI_CHAN_SUPPORT
	UCCP_DEBUG_TX("Q: %d qlen: %d status: %d chactx: %d out_tok: %d\n",
			  tx_done->queue,
			  qlen,
			  tx_done->frm_status[0],
			  curr_chanctx_idx,
			  dev->tx.outstanding_tokens[tx_done->queue]);
#else
	UCCP_DEBUG_TX("Q: %d qlen: %d status: %d out_tok: %d\n",
			  tx_done->queue,
			  qlen,
			  tx_done->frm_status[0],
			  dev->tx.outstanding_tokens[tx_done->queue]);
#endif

	update_aux_adc_voltage(dev, tx_done->pdout_voltage);

#ifdef MULTI_CHAN_SUPPORT
	if (tx_done->frm_status[0] == TX_DONE_STAT_DISCARD_CHSW) {
		pkts_pending = uccp420wlan_proc_tx_dscrd_chsw(dev,
							      curr_chanctx_idx,
							      tx_done);
		goto out;
	}
#endif
	pkts_pending = uccp420wlan_tx_free_buff_req(dev,
						    tx_done,
						    &queue,
#ifdef MULTI_CHAN_SUPPORT
						    curr_chanctx_idx,
#endif
						    &vif_index_bitmap);

	if (pkts_pending) {
		/*TODO..Do we need to check each skb for more_frames??*/
		more_frames = 0;

		UCCP_DEBUG_TX("%s-UMACTX:%s:%d Transfer Pending Frames:\n",
			       dev->name,
			       __func__,
			       __LINE__);

		ret = __uccp420wlan_tx_frame(dev,
					     queue,
					     token_id,
#ifdef MULTI_CHAN_SUPPORT
					     curr_chanctx_idx,
#endif
					     more_frames,
					     0);

	} else {
		DEBUG_LOG("%s-UMACTX:No Pending Packets\n", dev->name);
	}

#ifdef MULTI_CHAN_SUPPORT
out:
#endif

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

#ifdef MULTI_CHAN_SUPPORT
void uccp420_purge_tx_queue(struct mac80211_dev *dev,
			   struct sk_buff_head *skbs)
{


	struct sk_buff *loop_skb = NULL, *tmp = NULL;

	skb_queue_walk_safe(skbs,
			    loop_skb,
			    tmp) {
		struct ieee80211_hdr *hdr = NULL;

		__skb_unlink(loop_skb,
			     skbs);

		if (!loop_skb)
			continue;

		hdr = (struct ieee80211_hdr *)loop_skb->data;

		if (!ieee80211_is_beacon(hdr->frame_control))
			dev->stats->tx_dones_to_stack++;

		ieee80211_free_txskb(dev->hw,
				     loop_skb);
	}

}

static int uccp420_flush_vif_all_pend_q(struct mac80211_dev *dev,
					struct umac_vif *uvif,
					unsigned int hw_queue_map,
					enum UMAC_VIF_CHANCTX_TYPE chanctx_type)
{
	unsigned int pending = 0;
	int count = 0;
	unsigned int queue = 0;
	int pend_q = 0;
	struct sk_buff_head *pend_pkt_q = NULL;
	struct tx_config *tx = NULL;
	struct ieee80211_sta *sta = NULL;
	struct umac_sta *usta = NULL;
	bool warned = false;

	tx = &dev->tx;

	if (!uvif->chanctx) {
		UCCP_DEBUG_TX("%s: Chanctx NULL, returning\n",
						__func__);
		return -1;
	}

	for (queue = 0; queue < NUM_ACS; queue++) {
		if (!(BIT(queue) & hw_queue_map))
			continue;

		for (pend_q = 0; pend_q < MAX_PEND_Q_PER_AC; pend_q++) {
			rcu_read_lock();
			if (pend_q < MAX_PEERS) {
				sta = rcu_dereference(dev->peers[pend_q]);

				if (!sta) {
					rcu_read_unlock();
					continue;
				}

				usta = (struct umac_sta *)(sta->drv_priv);

				if (usta->vif_index != uvif->vif_index) {
					rcu_read_unlock();
					continue;
				}
				rcu_read_unlock();
			} else if (pend_q >= MAX_PEERS) {
				int vif_index;
				struct umac_vif *tvif;
				struct ieee80211_vif *vif = NULL;

				vif_index = pend_q - MAX_PEERS;
				vif = rcu_dereference(dev->vifs[vif_index]);

				if (!vif) {
					rcu_read_unlock();
					continue;
				}

				tvif = (struct umac_vif *)(vif->drv_priv);

				if (uvif->vif_index != tvif->vif_index) {
					rcu_read_unlock();
					continue;
				}
				rcu_read_unlock();
			} else {
				rcu_read_unlock();
				continue;
			}

			while (1) {
				spin_lock_bh(&tx->lock);

				pend_pkt_q =
					&tx->pending_pkt[chanctx_type]
							[pend_q]
							[queue];

				/* Assuming all packets for the peer have same
				 * channel context
				 */
				pending = skb_queue_len(pend_pkt_q);

				spin_unlock_bh(&tx->lock);

				if (!pending)
					break;

				if (!warned &&
				    count >= QUEUE_FLUSH_TIMEOUT_TICKS) {
					pr_err("%s: Timeout: VIF: %d Queue: %d pending: %d\n",
					       dev->name,
					       uvif->vif_index,
					       queue,
					       pending);
					WARN_ON(1);
					warned = true;
					return -1;
				}

				current->state = TASK_INTERRUPTIBLE;

				if (schedule_timeout(1) == 0)
					count++;

			}

			if (pending) {
				pr_err("%s-UMACTX: Failed for VIF: %d ",
					       dev->name,
					       uvif->vif_index);
				pr_err(" and Queue: %d, pending: %d\n",
					       queue,
					       pending);

				return -1;
			}
		}
	}

	UCCP_DEBUG_TX("%s: Success for VIF: %d and Queue: %d\n",
					__func__,
					uvif->vif_index,
					queue);
	return 0;
}

int uccp420_discard_sta_pend_q(struct mac80211_dev *dev,
				   struct umac_vif *uvif,
				   int peer_id,
				   unsigned int hw_queue_map)
{
	unsigned int pending = 0;
	unsigned int queue = 0;
	int pend_q = 0;
	struct sk_buff_head *pend_pkt_q = NULL, tx_discard_list;
	struct tx_config *tx = &dev->tx;

	UCCP_DEBUG_TX("%s:%d Enter..:tx:%d txd:%d hqmap:%d vif:%d\n",
		      __func__,
		      __LINE__,
		      dev->stats->tx_cmds_from_stack,
		      dev->stats->tx_dones_to_stack,
		      hw_queue_map,
		      uvif->vif_index);
	skb_queue_head_init(&tx_discard_list);

	for (queue = 0; queue < NUM_ACS; queue++) {

		if (!(BIT(queue) & hw_queue_map))
			continue;

		UCCP_DEBUG_TX("%s:%d MATCH queue: %d pend_q:%d\n",
			      __func__,
			      __LINE__,
			      queue,
			      pend_q);

		pend_pkt_q =
			&tx->pending_pkt[0]
					[peer_id]
					[queue];

		pending = skb_queue_len(pend_pkt_q);

		if (!pending)
			continue;

		UCCP_DEBUG_TX("%s:%d Free the skbs..\n",
			      __func__,
			      __LINE__);

		skb_queue_splice_tail_init(pend_pkt_q,
					   &tx_discard_list);
		uccp420_purge_tx_queue(dev, &tx_discard_list);

	}
	UCCP_DEBUG_TX("%s:%d Exit..:tx:%d txd:%d\n", __func__, __LINE__,
		      dev->stats->tx_cmds_from_stack,
		      dev->stats->tx_dones_to_stack);
	return 0;
}

int uccp420_discard_sta_tx_q(struct mac80211_dev *dev,
				   struct umac_vif *uvif,
				   int peer_id,
				   unsigned int hw_queue_map,
				   int chanctx_idx)
{
	struct tx_config *tx = &dev->tx;
	struct tx_pkt_info *pkt_info = NULL;
	struct sk_buff_head *txq = NULL;
	struct sk_buff *loop_skb = NULL, *tmp = NULL;
	int i = 0, pkt;

	UCCP_DEBUG_TX("%s:%d Enter..:tx:%d txd:%d hqmap:%d vif:%d\n",
		      __func__,
		      __LINE__,
		      dev->stats->tx_cmds_from_stack,
		      dev->stats->tx_dones_to_stack,
		      hw_queue_map,
		      uvif->vif_index);

	for (i = 0; i < NUM_TX_DESCS; i++) {
		pkt_info = &tx->pkt_info[chanctx_idx][i];
		UCCP_DEBUG_TX("%s:%d pvif: %d uvif:%d q:%d hq:%d peer_id:%d\n",
			      __func__,
			      __LINE__,
			      pkt_info->vif_index,
			      uvif->vif_index,
			      pkt_info->queue,
			      hw_queue_map,
			      peer_id);

		if ((pkt_info->vif_index == uvif->vif_index) &&
		    ((peer_id == -1) || (pkt_info->peer_id == peer_id)) &&
		    (BIT(pkt_info->queue) & hw_queue_map)) {
			int pool_id = i/TX_DESC_BUCKET_BOUND;

			txq = &pkt_info->pkt;

			if (!skb_queue_len(txq) ||
			    !test_bit(i, &tx->buf_pool_bmp[pool_id]))
				continue;

			UCCP_DEBUG_TX("%s: Free the skbs:%d\n", __func__, i);


			/* In the Tx path we move the .11hdr from skb to CMD_TX
			 * Hence pushing it here, not required for loopback case
			 */
			pkt = 0;
			skb_queue_walk_safe(txq,
					    loop_skb,
					    tmp) {
				skb_push(loop_skb, pkt_info->hdr_len);
				hal_ops.unmap_tx_buf(i, pkt);
				pkt++;
			}
			uccp420_purge_tx_queue(dev, txq);
			free_token(dev, i, pkt_info->queue);
			dev->tx.desc_chan_map[i] = -1;
		}
	}
	UCCP_DEBUG_TX("%s:%d Exit..:tx:%d txd:%d\n", __func__, __LINE__,
		      dev->stats->tx_cmds_from_stack,
		      dev->stats->tx_dones_to_stack);
	return 0;
}
static int uccp420_discard_vif_tx_queues(struct mac80211_dev *dev,
				       struct umac_vif *uvif,
				       int chanctx_idx,
				       unsigned int hw_queue_map)
{
	struct tx_config *tx = NULL;

	tx = &dev->tx;
	UCCP_DEBUG_TX("%s:%d Enter..:tx:%d txd:%d\n", __func__, __LINE__,
		      dev->stats->tx_cmds_from_stack,
		      dev->stats->tx_dones_to_stack);
	spin_lock_bh(&tx->lock);
	uccp420_discard_sta_tx_q(dev, uvif, -1, hw_queue_map, chanctx_idx);
	spin_unlock_bh(&tx->lock);


	UCCP_DEBUG_TX("%s: Success for VIF: %d",
					__func__,
					uvif->vif_index);
	UCCP_DEBUG_TX("%s:%d Enter..:tx:%d txd:%d\n", __func__, __LINE__,
		      dev->stats->tx_cmds_from_stack,
		      dev->stats->tx_dones_to_stack);
	return 0;

}
static int uccp420_flush_vif_tx_queues(struct mac80211_dev *dev,
				       struct umac_vif *uvif,
				       int chanctx_idx,
				       unsigned int hw_queue_map)
{
	unsigned int tokens = 0;
	unsigned int i = 0;
	unsigned long buf_pool_bmp = 0;
	struct tx_pkt_info *pkt_info = NULL;
	struct tx_config *tx = NULL;
	int count = 0;
	bool warned = false;

	tx = &dev->tx;

	spin_lock_bh(&tx->lock);

	for (i = 0; i < NUM_TX_DESCS; i++) {
		pkt_info = &tx->pkt_info[chanctx_idx][i];

		if ((pkt_info->vif_index == uvif->vif_index) &&
		    (BIT(pkt_info->queue) & hw_queue_map))
			tokens |= BIT(i);
	}

	spin_unlock_bh(&tx->lock);

	if (!tokens)
		return 0;

	while (1) {
		spin_lock_bh(&tx->lock);
		buf_pool_bmp = tx->buf_pool_bmp[0];
		spin_unlock_bh(&tx->lock);

		if (!(buf_pool_bmp & tokens))
			break;

		if (!warned &&
		    count >= QUEUE_FLUSH_TIMEOUT_TICKS) {
			pr_err("%s-UMACTX: TXQ: Failed for VIF: %d, buf_pool_bmp : 0x%lx:\n",
			       dev->name,
			       uvif->vif_index,
			       buf_pool_bmp);
			WARN_ON(1);
			warned = true;
			return -1;
		}

		current->state = TASK_INTERRUPTIBLE;

		if (schedule_timeout(1) == 0)
			count++;
	}

	UCCP_DEBUG_TX("%s: Success for VIF: %d, buf_pool_bmp : 0x%lx\n",
					__func__,
					uvif->vif_index,
					buf_pool_bmp);
	return 0;
}
int uccp420_discard_vif_all_pend_q(struct mac80211_dev *dev,
				   struct umac_vif *uvif,
				   unsigned int hw_queue_map)
{
	struct tx_config *tx = &dev->tx;
	struct ieee80211_sta *sta = NULL;
	struct umac_sta *usta = NULL;
	unsigned int pend_q;

	UCCP_DEBUG_TX("%s:%d Enter..:tx:%d txd:%d hqmap:%d vif:%d\n",
		      __func__,
		      __LINE__,
		      dev->stats->tx_cmds_from_stack,
		      dev->stats->tx_dones_to_stack,
		      hw_queue_map,
		      uvif->vif_index);
	spin_lock_bh(&tx->lock);

	for (pend_q = 0; pend_q < MAX_PEND_Q_PER_AC; pend_q++) {
		rcu_read_lock();
		if (pend_q < MAX_PEERS) {
			sta = rcu_dereference(dev->peers[pend_q]);

			if (!sta) {
				rcu_read_unlock();
				continue;
			}

			usta = (struct umac_sta *)(sta->drv_priv);

			if (usta->vif_index != uvif->vif_index) {
				rcu_read_unlock();
				continue;
			}
		} else if (pend_q >= MAX_PEERS) {
			int vif_index;
			struct umac_vif *tvif;
			struct ieee80211_vif *vif = NULL;

			vif_index = pend_q - MAX_PEERS;
			vif = rcu_dereference(dev->vifs[vif_index]);

			if (!vif) {
				rcu_read_unlock();
				continue;
			}

			tvif = (struct umac_vif *)(vif->drv_priv);

			if (uvif->vif_index != tvif->vif_index) {
				rcu_read_unlock();
				continue;
			}
		} else {
			rcu_read_unlock();
			continue;
		}
		rcu_read_unlock();
		uccp420_discard_sta_pend_q(dev, uvif, pend_q, hw_queue_map);
	}

	spin_unlock_bh(&tx->lock);
	UCCP_DEBUG_TX("%s:%d Exit..:tx:%d txd:%d\n", __func__, __LINE__,
		      dev->stats->tx_cmds_from_stack,
		      dev->stats->tx_dones_to_stack);
	return 0;
}

int uccp420_flush_vif_queues(struct mac80211_dev *dev,
			     struct umac_vif *uvif,
			     int chanctx_idx,
			     unsigned int hw_queue_map,
			     enum UMAC_VIF_CHANCTX_TYPE vif_chanctx_type,
			     bool drop)
{
	int result  = -1;
	char peer_addr[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

	UCCP_DEBUG_TX("%s:%d Enter..:tx:%d txd:%d\n", __func__, __LINE__,
		      dev->stats->tx_cmds_from_stack,
		      dev->stats->tx_dones_to_stack);

	if (drop) {
		/*Discard: clear pend_q, send tx_deinit to LMAC*/
		UCCP_DEBUG_TX("%s: DISCARD\n", __func__);
		result = uccp420_discard_vif_all_pend_q(dev,
						      uvif,
						      hw_queue_map);
		if (result)
			return result;

		dev->tx_deinit_complete = 0;
		uccp420wlan_prog_tx_deinit(uvif->vif_index, peer_addr);
		if (wait_for_tx_deinit_complete(dev) < 0) {
			/*Stuck: Reload FW??*/
			WARN_ON(1);
			result = uccp420_discard_vif_tx_queues(dev,
							       uvif,
							       chanctx_idx,
							       hw_queue_map);
		}
	} else {
		UCCP_DEBUG_TX("%s: FLUSH\n", __func__);
		/*Flush: Wait for frames*/
		result = uccp420_flush_vif_all_pend_q(dev,
						      uvif,
						      hw_queue_map,
						      vif_chanctx_type);

		if (result == 0) {
			result = uccp420_flush_vif_tx_queues(dev,
							     uvif,
							     chanctx_idx,
							     hw_queue_map);
		}

		if (result) {
			result = uccp420_discard_vif_all_pend_q(dev,
								uvif,
								hw_queue_map);

			result = uccp420_discard_vif_tx_queues(dev,
							     uvif,
							     chanctx_idx,
							     hw_queue_map);
		}
	}
	UCCP_DEBUG_TX("%s:%d Enter..:tx:%d txd:%d\n", __func__, __LINE__,
		      dev->stats->tx_cmds_from_stack,
		      dev->stats->tx_dones_to_stack);
	return result;
}
#endif
