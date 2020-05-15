/*
 * Copyright 2020 Paul Scherrer Institute
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

#include <unistd.h>
#include <malloc.h>
#include <iostream>
#include <arpa/inet.h>

#include "JFReceiver.h"

uint32_t lastModuleFrameNumber() {
    uint32_t retVal = online_statistics->head[0];
    for (int i = 1; i < NMODULES; i++) {
        if (online_statistics->head[i] < retVal) retVal = online_statistics->head[i];
    }
    if (retVal >= experiment_settings.nframes_to_collect - 1) return UINT32_MAX; // All frames were collected
    return retVal;
}


// Take half of the number, but only if not bad pixel/overload
inline int16_t half16(int16_t in) {
    int16_t tmp = in;
    if ((in > -32000) && (in < 32000)) tmp /= 2;
    return tmp;
}

// Take quarter of the number, but only if not bad pixel/overload
inline int16_t quarter16(int16_t in) {
    int16_t tmp = in;
    if ((in > -32000) && (in < 32000)) tmp /= 4;
    return tmp;
}

// Copy line and extend multipixels
void copy_line(int16_t *destination, int16_t* source) {
    for (int i = 0; i < 255; i++) destination[i] = source[i];
    for (int i = 1; i < 255; i++) destination[i+258] = source[i+256];
    for (int i = 1; i < 255; i++) destination[i+516] = source[i+512];
    for (int i = 1; i < 256; i++) destination[i+774] = source[i+768];
    for (int i = 0; i < 3; i++) {
        int16_t tmp1 = half16(source[255 + i*256]);
        destination[255+i*258] = tmp1;
        destination[256+i*258] = tmp1;
        int16_t tmp2 = half16(source[256 + i*256]);
        destination[257+i*258] = tmp2;
        destination[258+i*258] = tmp2;
    }
}

// Copy line with multi-pixels (255 and 256)
void copy_line_mid(int16_t *destination, int16_t* source, ssize_t offset) {
    for (int i = 0; i < 255; i++) {
         int16_t tmp = half16(source[i]);
         destination[i] = tmp;
         destination[i+offset] = tmp;
    }
    for (int i = 1; i < 255; i++) {
         int16_t tmp = half16(source[i+256]);
         destination[i+258] = tmp;
         destination[i+258+offset] = tmp;
    }

    for (int i = 1; i < 255; i++) {
         int16_t tmp = half16(source[i+512]);
         destination[i+516] = tmp;
         destination[i+516+offset] = tmp;
    }

    for (int i = 1; i < 256; i++) {
         int16_t tmp = half16(source[i+768]);
         destination[i+774] = tmp;
         destination[i+774+offset] = tmp;
    }

    for (int i = 0; i < 3; i++) {
        int16_t tmp1 = quarter16(source[255 + i*256]);
        destination[255+i*258] = tmp1;
        destination[256+i*258] = tmp1;
        destination[255+i*258+offset] = tmp1;
        destination[256+i*258+offset] = tmp1;
        int16_t tmp2 = quarter16(source[256 + i*256]);
        destination[257+i*258] = tmp2;
        destination[258+i*258] = tmp2;
        destination[257+i*258+offset] = tmp2;
        destination[258+i*258+offset] = tmp2;
    }
}


inline int32_t half32(int32_t in) {
    int32_t tmp = in;
    if ((in > INT32_MIN) && (in < INT32_MAX)) tmp /= 2;
    return tmp;
}

inline int32_t quarter32(int32_t in) {
    int32_t tmp = in;
    if ((in > INT32_MIN) && (in < INT32_MAX)) tmp /= 4;
    return tmp;
}

void copy_line32(int32_t *destination, int32_t* source) {
    for (int i = 0; i < 255; i++) destination[i] = source[i];
    for (int i = 1; i < 255; i++) destination[i+258] = source[i+256];
    for (int i = 1; i < 255; i++) destination[i+516] = source[i+512];
    for (int i = 1; i < 256; i++) destination[i+774] = source[i+768];
    for (int i = 0; i < 3; i++) {
        int32_t tmp1 = half32(source[255 + i*256]);
        destination[255+i*258] = tmp1;
        destination[256+i*258] = tmp1;
        int32_t tmp2 = half32(source[256 + i*256]);
        destination[257+i*258] = tmp2;
        destination[258+i*258] = tmp2;
    }
}

void copy_line_mid32(int32_t *destination, int32_t* source, size_t offset) {
    for (int i = 0; i < 255; i++) {
         int32_t tmp = half32(source[i]);
         destination[i] = tmp;
         destination[i+offset] = tmp;
    }
    for (int i = 1; i < 255; i++) {
         int32_t tmp = half32(source[i+256]);
         destination[i+258] = tmp;
         destination[i+258+offset] = tmp;
    }

    for (int i = 1; i < 255; i++) {
         int32_t tmp = half32(source[i+512]);
         destination[i+516] = tmp;
         destination[i+516+offset] = tmp;
    }

    for (int i = 1; i < 256; i++) {
         int32_t tmp = half32(source[i+768]);
         destination[i+774] = tmp;
         destination[i+774+offset] = tmp;
    }

    for (int i = 0; i < 3; i++) {
        int32_t tmp1 = quarter32(source[255 + i*256]);
        destination[255+i*258] = tmp1;
        destination[256+i*258] = tmp1;
        destination[255+i*258+offset] = tmp1;
        destination[256+i*258+offset] = tmp1;
        int32_t tmp2 = quarter32(source[256 + i*256]);
        destination[257+i*258] = tmp2;
        destination[258+i*258] = tmp2;
        destination[257+i*258+offset] = tmp2;
        destination[258+i*258+offset] = tmp2;
    }
}

void *run_poll_cq_thread(void *in_threadarg) {
	for (size_t finished_wc = 0; finished_wc < experiment_settings.nframes_to_write; finished_wc++) {
		// Poll CQ to reuse ID
		ibv_wc ib_wc;
		int num_comp  = ibv_poll_cq(ib_settings.cq, 1, &ib_wc);; // number of completions present in the CQ
		while (num_comp == 0) {
			usleep(100);
			num_comp = ibv_poll_cq(ib_settings.cq, 1, &ib_wc);
		}

		if (num_comp < 0) {
			std::cerr << "Failed polling IB Verbs completion queue" << std::endl;
			pthread_exit(0);
		}

		if (ib_wc.status != IBV_WC_SUCCESS) {
			std::cerr << "Failed status " << ibv_wc_status_str(ib_wc.status) << " of IB Verbs send request #" << (int)ib_wc.wr_id << std::endl;
			pthread_exit(0);
		}

                // Copy frame to GPU
//                if (receiver_settings.use_gpu && (experiment_settings.pixel_depth == 2))
//                    copy_to_gpu(ib_buffer + COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth * ib_wc.wr_id, ib_buffer_occupancy[ib_wc.wr_id]);

		pthread_mutex_lock(&ib_buffer_occupancy_mutex);
		ib_buffer_occupancy[ib_wc.wr_id] = 0;
		pthread_cond_signal(&ib_buffer_occupancy_cond);
		pthread_mutex_unlock(&ib_buffer_occupancy_mutex);
	}
	pthread_exit(0);
}

void *run_send_thread(void *in_threadarg) {
    ThreadArg *arg = (ThreadArg *) in_threadarg;

    pthread_mutex_lock(&trigger_frame_mutex);
//    if (experiment_settings.ntrigger > 0) trigger_frame = 0;
//    else {
        if (arg->ThreadID == 0) {
    	    while (online_statistics->trigger_position < experiment_settings.pedestalG0_frames) usleep(1000);
    	    trigger_frame = online_statistics->trigger_position;
    	    pthread_cond_broadcast(&trigger_frame_cond);
    	    std::cout << "Trigger observed at frame " << trigger_frame << std::endl;
            // Put warning if trigger too late
            if (experiment_settings.nframes_to_write * experiment_settings.summation + trigger_frame > experiment_settings.nframes_to_collect)
            std::cerr << "WARNING: Trigger observed too late and some frames at the end of dataset will not be collected" << std::endl;
        } else {
    	    while (online_statistics->trigger_position < experiment_settings.pedestalG0_frames)
    	    	pthread_cond_wait(&trigger_frame_cond, &trigger_frame_mutex);
        }
//    }
    pthread_mutex_unlock(&trigger_frame_mutex);

    uint32_t current_frame_number = lastModuleFrameNumber();

    int current_gpu_slice = 0;
    for (size_t frame = arg->ThreadID;
    		frame < experiment_settings.nframes_to_write;
    		frame += receiver_settings.compression_threads) {
        if (receiver_settings.use_gpu) {
            if (current_gpu_slice != frame / NFRAMES_PER_STREAM) {
                int finished_cuda_stream = current_gpu_slice % (NCUDA_STREAMS*CUDA_TO_IB_BUFFER);

                // Decrement counter of writers done
                pthread_mutex_lock(writer_threads_done_mutex+finished_cuda_stream);
                writer_threads_done[finished_cuda_stream] --;
                if (writer_threads_done[finished_cuda_stream] == 0)
                    pthread_cond_signal(writer_threads_done_cond+finished_cuda_stream);
                pthread_mutex_unlock(writer_threads_done_mutex+finished_cuda_stream);

                current_gpu_slice = frame / NFRAMES_PER_STREAM;
                int new_cuda_stream = current_gpu_slice % (NCUDA_STREAMS*CUDA_TO_IB_BUFFER);

                // Make sure that CUDA stream is ready to go
                pthread_mutex_lock(cuda_stream_ready_mutex+new_cuda_stream);
                while (cuda_stream_ready[new_cuda_stream] == 0)
                    pthread_cond_wait(cuda_stream_ready_cond+new_cuda_stream,
                                      cuda_stream_ready_mutex+new_cuda_stream);
                cuda_stream_ready[new_cuda_stream]--;
                pthread_mutex_unlock(cuda_stream_ready_mutex+new_cuda_stream);
            }
        }
    	// Find free buffer to write
    	int32_t buffer_id;

        // If pixel_depth == 4, then only half of buffer size available
        if  (experiment_settings.pixel_depth == 2) buffer_id = frame % (RDMA_SQ_SIZE);
        else  buffer_id = frame % (RDMA_SQ_SIZE / 2);

    	pthread_mutex_lock(&ib_buffer_occupancy_mutex);

        while ((ib_buffer_occupancy[buffer_id] != 0))
            pthread_cond_wait(&ib_buffer_occupancy_cond, &ib_buffer_occupancy_mutex);

    	pthread_mutex_unlock(&ib_buffer_occupancy_mutex);

        size_t collected_frame = frame*experiment_settings.summation + trigger_frame;

        if (current_frame_number < (collected_frame+experiment_settings.summation-1) + 3) {
    	// Ensure that all frames were already collected, if not wait 0.5 ms to try again
    	    while (current_frame_number < (collected_frame+experiment_settings.summation-1) + 3) {
                usleep(500);
               current_frame_number = lastModuleFrameNumber();
            }
        }

        if (frame % 100 == 0) {
           std::cout << "Frame :" << frame << " Backlog = " << current_frame_number - (collected_frame+experiment_settings.summation-1) << " " << online_statistics->head[0] << " " << online_statistics->head[1] << " " << online_statistics->head[2] << " " << online_statistics->head[3] << " " << online_statistics->good_packets << std::endl;
        }

        if (experiment_settings.conversion_mode == MODE_CONV) {
          // Expand multi-pixels and switch to 2x2 modules settings
          if (experiment_settings.summation == 1) {
            // For summation of 1 buffer is 16-bit, otherwise 32-bit
            int16_t *output_buffer = (int16_t *) (ib_buffer + buffer_id * COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth);
            // Correct geometry to make 2x2 module arrangement
            // Add inter-chip gaps
            // Inter module gaps are not added and should be corrected in processing software
            for (int module = 0; module < NMODULES; module ++) {
                size_t pixel_in  = ((collected_frame % FRAME_BUF_SIZE) * NMODULES + module) * MODULE_LINES * MODULE_COLS;
                size_t line_out = (module / 2) * 514; // Two modules will get the same line
                line_out = 514 * NMODULES / 2 - line_out - 1; // Flip upside down
                size_t pixel_out = (line_out * 2  + (module%2)) * 1030; // 2 modules in one row

                for (uint64_t i = 0; i < MODULE_LINES; i ++) {
                    if ((i == 255) || (i == 256)) {
                       pixel_out -= 2 * 1030;
                       copy_line_mid(output_buffer+pixel_out, frame_buffer + pixel_in, 2*1030);
                       pixel_out -= 2 * 1030;
                    } else {
                       copy_line(output_buffer+pixel_out, frame_buffer + pixel_in);
                       pixel_out -= 2 * 1030;
                    }
                    pixel_in += MODULE_COLS;
               }
            }
          } else {
            // For summation of >= 2 32-bit integers are used
            int32_t *output_buffer = (int32_t *) (ib_buffer + buffer_id * COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth);
            for (int module = 0; module < NMODULES; module ++) {
                size_t line_out = (module / 2) * 514;
                line_out = 514 * NMODULES / 2 - line_out - 1; // Flip upside down
                size_t pixel_out = (line_out * 2 + (module % 2)) * 1030; // 2 modules in one row

                for (uint64_t line = 0; line < MODULE_LINES; line ++) {
                    int32_t summed_buffer[MODULE_COLS];
		    for (int col = 0; col < MODULE_COLS; col++)
                        summed_buffer[col] = 0;

                    for (int j = 0; j < experiment_settings.summation; j++) {
                        size_t pixel0_in = ((((collected_frame + j) % FRAME_BUF_SIZE) * NMODULES +  module) * MODULE_LINES + line ) * MODULE_COLS;
                        for (int col = 0; col < MODULE_COLS; col++) {
                            int16_t tmp = frame_buffer[pixel0_in + col];
                            if (tmp < -32000) summed_buffer[col] = INT32_MIN;  
                            if ((tmp > 32000) && (summed_buffer[col] != INT32_MIN)) summed_buffer[col] = INT32_MAX;
                            if ((summed_buffer[col] != INT32_MIN) && (summed_buffer[col] != INT32_MAX)) summed_buffer[col] += tmp;  
                        }
                    }
                    if ((line == 255) || (line == 256)) {
                       pixel_out -= 2 * 1030;
                       copy_line_mid32(output_buffer+pixel_out, summed_buffer, 2*1030);
                       pixel_out -= 2 * 1030;
                    } else {
                       copy_line32(output_buffer+pixel_out, summed_buffer);
                       pixel_out -= 2 * 1030;
                    }
                }
            }
          }
        } else
            // For raw data, just copy contest of the buffer
            memcpy(ib_buffer + COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth * buffer_id,
                   frame_buffer + (collected_frame % FRAME_BUF_SIZE) * NPIXEL, NPIXEL * sizeof(uint16_t));


    	// Send the frame via RDMA
    	ibv_sge ib_sg;
    	ibv_send_wr ib_wr;
    	ibv_send_wr *ib_bad_wr;

    	memset(&ib_sg, 0, sizeof(ib_sg));
    	ib_sg.addr	 = (uintptr_t)(ib_buffer + COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth * buffer_id);
        if (experiment_settings.conversion_mode == MODE_CONV)
                ib_sg.length = COMPOSED_IMAGE_SIZE * experiment_settings.pixel_depth;
        else ib_sg.length = NPIXEL * sizeof(uint16_t);

        ib_sg.lkey	 = ib_settings.buffer_mr->lkey;

    	memset(&ib_wr, 0, sizeof(ib_wr));
    	ib_wr.wr_id      = buffer_id;
    	ib_wr.sg_list    = &ib_sg;
    	ib_wr.num_sge    = 1;
    	ib_wr.opcode     = IBV_WR_SEND_WITH_IMM;
    	ib_wr.send_flags = IBV_SEND_SIGNALED;
        ib_wr.imm_data   = htonl(frame); // Network order
        int ret;
    	while ((ret = ibv_post_send(ib_settings.qp, &ib_wr, &ib_bad_wr))) {
                if (ret != ENOMEM)
//    		std::cerr << "Sending with IB Verbs failed (ret: ENONEM buffer: " << buffer_id << " len: " << ib_sg.length << ")" << std::endl;
//                else
    		std::cerr << "Sending with IB Verbs failed (ret: " << ret << " buffer: " << buffer_id << " len: " << ib_sg.length << ")" << std::endl;
                usleep(200);
    	}
    }
    std::cout << arg->ThreadID << ": Sending done" << std::endl;
    pthread_exit(0);
}