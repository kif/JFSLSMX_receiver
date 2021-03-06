/*
 * Copyright 2017 International Business Machines
 * Copyright 2019 Paul Scherrer Institute
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include "ap_int.h"
#include "ap_utils.h"

#include "hw_action_rx100G.h"

packed_pedeG0_t packed_pedeG0[NPIXEL / 32];
ap_uint<32> pixel_mask[NPIXEL / 32];

void copy_data(rx100g_mem_t *din_gmem, rx100g_hbm_t *d_hbm0, rx100g_hbm_t *d_hbm1, size_t in_addr) {
	for (size_t i = 0; i < NMODULES * 512 * 1024 / 32; i ++) {
		ap_uint<512> tmp = din_gmem[in_addr+i];
		d_hbm0[i] = tmp(255,0);
	}
	for (size_t i = 0; i < NMODULES * 512 * 1024 / 32; i ++) {
		ap_uint<512> tmp = din_gmem[in_addr+i];
		d_hbm1[i] = tmp(511,256);
	}
}

void load_data_to_hbm(rx100g_mem_t *din_gmem, uint64_t in_gain_pedestal_addr,
		rx100g_hbm_t *d_hbm_p0, rx100g_hbm_t *d_hbm_p1,
		rx100g_hbm_t *d_hbm_p2, rx100g_hbm_t *d_hbm_p3,
		rx100g_hbm_t *d_hbm_p4, rx100g_hbm_t *d_hbm_p5,
		rx100g_hbm_t *d_hbm_p6, rx100g_hbm_t *d_hbm_p7,
		rx100g_hbm_t *d_hbm_p8, rx100g_hbm_t *d_hbm_p9,
		rx100g_hbm_t *d_hbm_p10, rx100g_hbm_t *d_hbm_p11) {
	size_t offset = in_gain_pedestal_addr;
	for (size_t i = 0; i < NPIXEL * 2 / 64; i ++) {
#pragma HLS PIPELINE
		ap_uint<512> tmp = din_gmem[offset+i];
		d_hbm_p0[i] = tmp(255,0);
		d_hbm_p1[i] = tmp(511,256);
	}
	offset += NPIXEL * 2 / 64;
	for (size_t i = 0; i < NPIXEL * 2 / 64; i ++) {
#pragma HLS PIPELINE
		ap_uint<512> tmp = din_gmem[offset+i];
		d_hbm_p2[i] = tmp(255,0);
		d_hbm_p3[i] = tmp(511,256);
	}
	offset += NPIXEL * 2 / 64;
	for (size_t i = 0; i < NPIXEL * 2 / 64; i ++) {
#pragma HLS PIPELINE
		ap_uint<512> tmp = din_gmem[offset+i];
		d_hbm_p4[i] = tmp(255,0);
		d_hbm_p5[i] = tmp(511,256);
	}
	offset += NPIXEL * 2 / 64;
	for (size_t i = 0; i < NPIXEL * 2 / 64; i ++) {
#pragma HLS PIPELINE
		ap_uint<512> tmp = din_gmem[offset+i];
		d_hbm_p6[i] = tmp(255,0);
		d_hbm_p7[i] = tmp(511,256);
	}
	offset += NPIXEL * 2 / 64;
	for (size_t i = 0; i < NPIXEL * 2 / 64; i ++) {
#pragma HLS PIPELINE
		ap_uint<512> tmp = din_gmem[offset+i];
		d_hbm_p8[i] = tmp(255,0);
		d_hbm_p9[i] = tmp(511,256);
	}
	// p10 and p11 are used for statistics and need to be zeroed out at the beginning
	for (size_t i = 0; i < NPIXEL * 2 / 64; i ++) {
#pragma HLS PIPELINE
		d_hbm_p10[i] = 0;
		d_hbm_p11[i] = 0;
	}
}

#define BURST_SIZE 4

void save_pedestal(rx100g_mem_t *dout_gmem, size_t offset) {
	for (size_t i = 0; i < NPIXEL * 2 / 64 / BURST_SIZE; i ++) {
#pragma HLS PIPELINE II = 4

		packed_pedeG0_t tmp[BURST_SIZE];
		ap_uint<512> tmp2[BURST_SIZE];

		for (int j = 0; j < BURST_SIZE; j++) tmp[j] = packed_pedeG0[BURST_SIZE*i+j];
		for (int j = 0; j < BURST_SIZE; j++) pack_pede(tmp[j],tmp2[j]);
		memcpy(dout_gmem+offset+BURST_SIZE*i, tmp2, BURST_SIZE*64);
	}
}

void load_pedestal(rx100g_mem_t *din_gmem, size_t offset) {
	for (size_t i = 0; i < NPIXEL * 2 / 64 / BURST_SIZE; i ++) {
#pragma HLS PIPELINE II = 4
		packed_pedeG0_t tmp[BURST_SIZE];
		ap_uint<512> tmp2[BURST_SIZE];
		memcpy(tmp2, din_gmem+offset+BURST_SIZE*i, BURST_SIZE*64);
		for (int j = 0; j < BURST_SIZE; j++) {
			pedeG1G2_t in[32];
			pedeG0_t out[32];
			unpack_pedeG1G2(tmp2[j],in);
			for (int k = 0; k < 32; k++ ) out[k] = in[k];
			pack_pedeG0(tmp[j], out);
		}
		for (int j = 0; j < BURST_SIZE; j++) packed_pedeG0[BURST_SIZE*i+j] = tmp[j];
	}
}

void clear_pixel_mask() {
	for (size_t i = 0; i < NPIXEL / 32; i ++)
#pragma HLS unroll factor=4
		pixel_mask[i] = 0;

}

void update_pixel_mask(rx100g_mem_t *din_gmem, size_t offset, size_t bit) {
	for (size_t i = 0; i < NPIXEL / 32 / BURST_SIZE; i ++) {
#pragma HLS pipeline
		ap_uint<512> tmp[BURST_SIZE];
		memcpy(tmp, din_gmem+offset+i*BURST_SIZE, 64*BURST_SIZE);
		for (int k = 0; k < BURST_SIZE; k++)
			for (int j = 0; j < 32; j++) {
				if (pixel_mask[i*BURST_SIZE+k][j] == 1) tmp[k][j*16+bit] = 1;
				else tmp[k][j*16+bit] = 0;
			}
		memcpy(din_gmem+offset+i*BURST_SIZE, tmp, 64*BURST_SIZE);
	}
}

// Taken from HBM_memcopy action
//convert buffer 256b to 512b
static void HBMbus_to_membus(rx100g_hbm_t *data_in, rx100g_mem_t *data_out,
		uint64_t size_in_words_512)
{
#pragma HLS INLINE off
	static rx100g_mem_t data_entry = 0;

	hbm2mem_loop:
	for (int k=0; k<size_in_words_512; k++) {
#pragma HLS PIPELINE II=2
		for (int j = 0; j < 2; j++) {
			data_entry |= ((rx100g_mem_t)(data_in[k*2+j])) << j*MEMDW/2;
		}
		data_out[k] = data_entry;
		data_entry = 0;
	}
}


void make_packet(AXI_STREAM &din_eth, uint64_t frame_number, uint32_t eth_packet, uint16_t module, uint16_t *data) {
	char buff[130*64];
	RAW_JFUDP_Packet *packet = (RAW_JFUDP_Packet *)buff;
	ap_uint<512> *obuff = (ap_uint<512> *)buff;
	packet->ether_type = 0x0008;
	packet->sour_mac[0] = 0x00; // module 0

	packet->dest_mac[0] = 0xAA; // Big endian in IP header!
	packet->dest_mac[1] = 0xBB;
	packet->dest_mac[2] = 0xCC;
	packet->dest_mac[3] = 0xDD;
	packet->dest_mac[4] = 0xEE;
	packet->dest_mac[5] = 0xF1;
	packet->ipv4_header_h = 0x45; // Big endian in IP header!
	packet->ipv4_header_total_length = 0x4C20; // Big endian in IP header!
	packet->ipv4_header_dest_ip = 0x0532010A; // Big endian in IP header!

	if (eth_packet > 63)
		packet->ipv4_header_sour_ip = 0x0632010A;
	else
		packet->ipv4_header_sour_ip = 0x0732010A;

	packet->ipv4_header_ttl_protocol = 0x1100;
	packet->udp_dest_port = module + 0xC0CC; // module number
	packet->udp_sour_port = 0xACDF;
	packet->timestamp = 0xA0A0A0A0;
	packet->framenum = frame_number;
	packet->packetnum = eth_packet % 64;

	for (int i = 0; i < 4096; i++) packet->data[i] = data[i];

	//__hexdump(stdout, obuff,64*130);
	ap_axiu_for_eth packet_in;

	for (int i = 0; i < 130; i++) {
		if (i == 129) packet_in.last = 1;
		else packet_in.last = 0;
		packet_in.keep = 0xFFFFFFFFFFFFFFFF;
		packet_in.user = 0; // TODO: Check 1
		packet_in.data = obuff[i];
		din_eth.write(packet_in);
	}

}


void collect_data(AXI_STREAM &din_eth,
		eth_settings_t eth_settings,
		rx100g_mem_t *dout_gmem,
		size_t out_frame_buffer_addr, size_t out_frame_status_addr,
		rx100g_hbm_t *d_hbm_p0, rx100g_hbm_t *d_hbm_p1,
		rx100g_hbm_t *d_hbm_p2, rx100g_hbm_t *d_hbm_p3,
		rx100g_hbm_t *d_hbm_p4, rx100g_hbm_t *d_hbm_p5,
		rx100g_hbm_t *d_hbm_p6, rx100g_hbm_t *d_hbm_p7,
		rx100g_hbm_t *d_hbm_p8, rx100g_hbm_t *d_hbm_p9,
		rx100g_hbm_t *d_hbm_p10, rx100g_hbm_t *d_hbm_p11,
		conversion_settings_t conversion_settings, ap_uint<2> select_output) {

#pragma HLS DATAFLOW

	DATA_STREAM raw;
#pragma HLS STREAM variable=raw
#pragma HLS RESOURCE variable=raw CORE=FIFO_LUTRAM

	DATA_STREAM after_pedeG0;
#pragma HLS STREAM variable=after_pedeG0 depth=512

	DATA_STREAM converted;
#pragma HLS STREAM variable=converted depth=512

	// 1. Read packet from 100G Ethernet
	read_eth_packet(din_eth, raw, eth_settings, d_hbm_p10);

	// 2. Update pedestal (for any gain) and apply G0 pedestal
	pedestalG0(raw, after_pedeG0, conversion_settings);

	// 3. Apply gain correction to G0 pixels
	apply_gain_correction(after_pedeG0, converted,
			d_hbm_p0, d_hbm_p1,
			d_hbm_p2, d_hbm_p3,
			d_hbm_p4, d_hbm_p5,
			d_hbm_p6, d_hbm_p7,
			d_hbm_p8, d_hbm_p9,
			select_output);

	// 4. Write raw or converted data to host memory
	write_data(converted, dout_gmem, out_frame_buffer_addr, out_frame_status_addr, d_hbm_p11);
}

//----------------------------------------------------------------------
//--- MAIN PROGRAM -----------------------------------------------------
//----------------------------------------------------------------------
static int process_action(rx100g_mem_t *din_gmem,
		rx100g_mem_t *dout_gmem,
		rx100g_hbm_t *d_hbm_p0, rx100g_hbm_t *d_hbm_p1,
		rx100g_hbm_t *d_hbm_p2, rx100g_hbm_t *d_hbm_p3,
		rx100g_hbm_t *d_hbm_p4, rx100g_hbm_t *d_hbm_p5,
		rx100g_hbm_t *d_hbm_p6, rx100g_hbm_t *d_hbm_p7,
		rx100g_hbm_t *d_hbm_p8, rx100g_hbm_t *d_hbm_p9,
		rx100g_hbm_t *d_hbm_p10, rx100g_hbm_t *d_hbm_p11,
		AXI_STREAM &din_eth,
		AXI_STREAM &dout_eth,
		action_reg *act_reg)
{

	// HBM and in-memory order:
	// p0,p1 - gain G0
	// p2,p3 - gain G1
	// p4,p5 - gain G2
	// p6,p7 - pedestal G1
	// p8,p9 - pedestal G2
	// p10   - JF packet headers
	// p11   - frame counts

	// Send ARP information
	send_gratious_arp(dout_eth, act_reg->Data.fpga_mac_addr, act_reg->Data.fpga_ipv4_addr);

	// Fill data structures
	size_t in_gain_pedestal_addr = act_reg->Data.in_gain_pedestal_data_addr >> ADDR_RIGHT_SHIFT;
	size_t out_frame_buffer_addr = act_reg->Data.out_frame_buffer_addr >> ADDR_RIGHT_SHIFT;
	size_t out_frame_status_addr = act_reg->Data.out_frame_status_addr >> ADDR_RIGHT_SHIFT;
	size_t jf_packet_headers_addr = act_reg->Data.out_jf_packet_headers_addr >> ADDR_RIGHT_SHIFT;

	eth_settings_t eth_settings;
	eth_settings.fpga_mac_addr = act_reg->Data.fpga_mac_addr;
	eth_settings.fpga_ipv4_addr = act_reg->Data.fpga_ipv4_addr;
	eth_settings.frame_number_to_stop = act_reg->Data.expected_frames;
	eth_settings.frame_number_to_quit = act_reg->Data.expected_frames + DELAY_FRAMES_STOP_AND_QUIT;
	eth_settings.first_frame_number = act_reg->Data.first_frame_number;
        eth_settings.expected_triggers = act_reg->Data.expected_triggers;
        eth_settings.frames_per_trigger = act_reg->Data.frames_per_trigger;
        eth_settings.delay_per_trigger = act_reg->Data.delay_per_trigger;

        if (act_reg->Data.mode == MODE_CONV)
            eth_settings.pedestalG0_frames = act_reg->Data.pedestalG0_frames;
        else eth_settings.pedestalG0_frames = 0; // This is only necessary for conversion


	conversion_settings_t conversion_settings;

	conversion_settings.conversion_mode = act_reg->Data.mode;
	conversion_settings.tracking_threshold = 0;

	ap_uint<2> output_type;
	if (act_reg->Data.mode == MODE_CONV) output_type = OUTPUT_CONV;
	else output_type = OUTPUT_RAW;

	// Load constants
	load_data_to_hbm(din_gmem, in_gain_pedestal_addr,
			d_hbm_p0, d_hbm_p1,
			d_hbm_p2, d_hbm_p3,
			d_hbm_p4, d_hbm_p5,
			d_hbm_p6, d_hbm_p7,
			d_hbm_p8, d_hbm_p9,
			d_hbm_p10, d_hbm_p11);

	// Load pedestal estimation and pixel_mask into main memory
	switch (conversion_settings.conversion_mode) {
	case MODE_PEDEG0:
	case MODE_CONV:
		load_pedestal(din_gmem, in_gain_pedestal_addr + 5 * NPIXEL * 2L / 64);
		break;
	case MODE_PEDEG1:
		load_pedestal(din_gmem, in_gain_pedestal_addr + 3 * NPIXEL * 2L / 64);
		break;
	case MODE_PEDEG2:
		load_pedestal(din_gmem, in_gain_pedestal_addr + 4 * NPIXEL * 2L / 64);
		break;
	}

	// Set pixel mask to zero
	clear_pixel_mask();

	// Run data collection
	collect_data(din_eth, eth_settings, dout_gmem, out_frame_buffer_addr, out_frame_status_addr,
			d_hbm_p0, d_hbm_p1,
			d_hbm_p2, d_hbm_p3,
			d_hbm_p4, d_hbm_p5,
			d_hbm_p6, d_hbm_p7,
			d_hbm_p8, d_hbm_p9,
			d_hbm_p10, d_hbm_p11,
			conversion_settings, output_type);

	// Save calculated pedestal back to memory
	switch (conversion_settings.conversion_mode) {
	case MODE_PEDEG0:
	case MODE_CONV:
		save_pedestal(dout_gmem, in_gain_pedestal_addr + 5 * NPIXEL * 2L / 64);
		update_pixel_mask(din_gmem, in_gain_pedestal_addr + 6 * NPIXEL * 2L / 64, 1);
		break;
	case MODE_PEDEG1:
		save_pedestal(dout_gmem, in_gain_pedestal_addr + 3 * NPIXEL * 2L / 64);
		update_pixel_mask(din_gmem, in_gain_pedestal_addr + 6 * NPIXEL * 2L / 64, 2);
		break;
	case MODE_PEDEG2:
		save_pedestal(dout_gmem, in_gain_pedestal_addr + 4 * NPIXEL * 2L / 64);
		update_pixel_mask(din_gmem, in_gain_pedestal_addr + 6 * NPIXEL * 2L / 64, 3);
		break;
	}

	// Save JF packet headers
	HBMbus_to_membus(d_hbm_p10,  dout_gmem + jf_packet_headers_addr,
			(act_reg->Data.expected_frames*NMODULES)/2);

	// Save JF status bits
	HBMbus_to_membus(d_hbm_p11,  dout_gmem + out_frame_status_addr + 1,
			(act_reg->Data.expected_frames*NMODULES*16)/64);

	act_reg->Control.Retc = SNAP_RETC_SUCCESS;

	return 0;
}

//--- TOP LEVEL MODULE -------------------------------------------------
void hls_action(rx100g_mem_t *din_gmem, rx100g_mem_t *dout_gmem,
		rx100g_hbm_t *d_hbm_p0, rx100g_hbm_t *d_hbm_p1,
		rx100g_hbm_t *d_hbm_p2, rx100g_hbm_t *d_hbm_p3,
		rx100g_hbm_t *d_hbm_p4, rx100g_hbm_t *d_hbm_p5,
		rx100g_hbm_t *d_hbm_p6, rx100g_hbm_t *d_hbm_p7,
		rx100g_hbm_t *d_hbm_p8, rx100g_hbm_t *d_hbm_p9,
		rx100g_hbm_t *d_hbm_p10, rx100g_hbm_t *d_hbm_p11,
		AXI_STREAM &din_eth, AXI_STREAM &dout_eth, volatile ap_uint<1> &eth_reset,
#ifdef OCACCEL
		action_reg *act_reg)
#else
action_reg *act_reg,
action_RO_config_reg *Action_Config)
#endif
		{
			// Host Memory AXI Interface - CANNOT BE REMOVED - NO CHANGE BELOW
#pragma HLS INTERFACE m_axi port=din_gmem bundle=host_mem offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64
#pragma HLS INTERFACE s_axilite port=din_gmem bundle=ctrl_reg offset=0x030

#pragma HLS INTERFACE m_axi port=dout_gmem bundle=host_mem offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=384
#pragma HLS INTERFACE s_axilite port=dout_gmem bundle=ctrl_reg offset=0x040

			/*  // DDR memory Interface - CAN BE COMMENTED IF UNUSED
			 * #pragma HLS INTERFACE m_axi port=d_ddrmem bundle=card_mem0 offset=slave depth=512 \
			 *   max_read_burst_length=64  max_write_burst_length=64
			 * #pragma HLS INTERFACE s_axilite port=d_ddrmem bundle=ctrl_reg offset=0x050
			 */
			// Host Memory AXI Lite Master Interface - NO CHANGE BELOW

#ifndef OCACCEL
#pragma HLS DATA_PACK variable=Action_Config
#pragma HLS INTERFACE s_axilite port=Action_Config bundle=ctrl_reg offset=0x010
#endif

#pragma HLS DATA_PACK variable=act_reg
#pragma HLS INTERFACE s_axilite port=act_reg bundle=ctrl_reg offset=0x100
#pragma HLS INTERFACE s_axilite port=return bundle=ctrl_reg

#pragma HLS INTERFACE m_axi port=d_hbm_p0 bundle=card_hbm_p0 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=40
#pragma HLS INTERFACE m_axi port=d_hbm_p1 bundle=card_hbm_p1 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=40
#pragma HLS INTERFACE m_axi port=d_hbm_p2 bundle=card_hbm_p2 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=40
#pragma HLS INTERFACE m_axi port=d_hbm_p3 bundle=card_hbm_p3 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=40
#pragma HLS INTERFACE m_axi port=d_hbm_p4 bundle=card_hbm_p4 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=40
#pragma HLS INTERFACE m_axi port=d_hbm_p5 bundle=card_hbm_p5 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=40
#pragma HLS INTERFACE m_axi port=d_hbm_p6 bundle=card_hbm_p6 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=40
#pragma HLS INTERFACE m_axi port=d_hbm_p7 bundle=card_hbm_p7 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=40
#pragma HLS INTERFACE m_axi port=d_hbm_p8 bundle=card_hbm_p8 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=40
#pragma HLS INTERFACE m_axi port=d_hbm_p9 bundle=card_hbm_p9 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=40
#pragma HLS INTERFACE m_axi port=d_hbm_p10 bundle=card_hbm_p10 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=40
#pragma HLS INTERFACE m_axi port=d_hbm_p11 bundle=card_hbm_p11 offset=slave depth=512 \
		max_read_burst_length=64  max_write_burst_length=64 latency=40

#pragma HLS INTERFACE axis register off port=din_eth
#pragma HLS INTERFACE axis register off port=dout_eth
#pragma HLS INTERFACE ap_none port=eth_reset

#pragma HLS RESOURCE variable=packed_pedeG0 core=RAM_1P_URAM
#pragma HLS ARRAY_PARTITION variable=packed_pedeG0 cyclic factor=4 dim=1

#pragma HLS RESOURCE variable=pixel_mask core=RAM_1P_URAM
#pragma HLS ARRAY_PARTITION variable=pixel_mask cyclic factor=4 dim=1

			/* Required Action Type Detection - NO CHANGE BELOW */
			//	NOTE: switch generates better vhdl than "if" */
			// Test used to exit the action if no parameter has been set.
			// Used for the discovery phase of the cards */
#ifndef OCACCEL
			switch (act_reg->Control.flags) {
			case 0:
				Action_Config->action_type = ACTION_TYPE; //TO BE ADAPTED
				Action_Config->release_level = RELEASE_LEVEL;
				act_reg->Control.Retc = 0xe00f;
				return;
				break;
			default:
#endif
				// TODO: High priority - need to have this reset by default at the beginning of the action
				{
#pragma HLS PROTOCOL fixed
					int i = 0;
					eth_reset = 1;
					while (i < 64) {
						i++; ap_wait();
					}
					if (i == 64) eth_reset = 0;
			}
				process_action(din_gmem, dout_gmem, 
                                               d_hbm_p0, d_hbm_p1, d_hbm_p2, d_hbm_p3, 
                                               d_hbm_p4, d_hbm_p5, d_hbm_p6, d_hbm_p7, 
                                               d_hbm_p8, d_hbm_p9, d_hbm_p10,d_hbm_p11, 
                                               din_eth, dout_eth, act_reg);			
#ifndef OCACCEL
				break;
			}
#endif
		}

