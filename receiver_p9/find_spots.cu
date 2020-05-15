#include <fstream>
#include <iostream>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <map>
#include <vector>
#include <pthread.h>
#include "JFReceiver.h"

#define MAX_STRONG 8192

#define NBX 3
#define NBY 3

#define COLS (2*1030)
#define LINES (514)

#define FRAME_SIZE ((NMODULES/2) * COLS * LINES * sizeof(int16_t))

cudaStream_t stream[NCUDA_STREAMS];

struct strong_pixel {
    int16_t col;
    int16_t line;
    uint32_t photons;
};


// GPU kernel to find strong pixels
__global__ void find_spots_colspot(int16_t *in, strong_pixel *out, float strong, int N) {
     if (blockIdx.x * blockDim.x + threadIdx.x < N) {
        // Threshold for signal^2 / var
        // To avoid division (see later) N/(N-1) factor is included already in the threshold
        float threshold = strong * strong * (float)((2*NBX+1) * (2*NBY+1)) / (float) ((2*NBX+1) * (2*NBY+1)-1);

        // One thread is 512 lines or 1 module
        // line0 points to the module/frame
        size_t line0 = (blockIdx.x * blockDim.x + threadIdx.x) * 512L;

        // Location of the first strong pixel in the output array 
        size_t strong_id0 = (blockIdx.x * blockDim.x + threadIdx.x) * MAX_STRONG;
        size_t strong_id = 0;

        // Sum and sum of squares of (2*NBY+1) vertical elements 
        // These are updated after each line is finished
        // 64-bit integer guarantees calculations are made without rounding errors
        int64_t sum_vert[COLS];
        int64_t sum2_vert[COLS];

        // Precalculate squares for first 2*NBY+1 lines
        for (int col = 0; col < COLS; col++) {
            sum_vert[col]  = in[(line0) * COLS + col];
            sum2_vert[col] = in[(line0) * COLS + col]*in[(line0) * COLS + col];
        }
 
        for (size_t line = 1; line < 2*NBY+1; line++) {
            for (int col = 0; col < COLS; col++) {
                sum_vert[col]  += in[(line0 + line) * COLS + col];
                sum2_vert[col] += in[(line0 + line) * COLS + col] * in[(line0 + line) * COLS + col];
            }
        }

        // do calculations for lines NBY to MODULE_LINES - NBY
        for (size_t line = NBY; line < LINES - NBY; line++) {

            // sum and sum of squares for (2*NBX+1) x (2*NBY+1) elements
            int64_t sum  = sum_vert[0];
            int64_t sum2 = sum2_vert[0];

            for (int i = 1; i < 2*NBX+1; i ++) {
                sum  += sum_vert[i];
                sum2 += sum2_vert[i];
            }

            for (int col = NBX; col < COLS - NBX; col++) {

                // At all cost division and sqrt must be avoided
                // as performance penalty is significant (2x drop)
                // instead, constants ((2*NBX+1) * (2*NBY+1)) and ((2*NBX+1) * (2*NBY+1)-1)
                // are included in the threshold
                float var = (2*NBX+1) * (2*NBY+1) * sum2 - (sum * sum); // This should be divided by (float) ((2*NBX+1) * (2*NBY+1)-1)

                float mean = sum; // Should be divided (float)((2*NBX+1) * (2*NBY+1));
                float in_minus_mean = in[(line0 + line)*COLS+col] * (float)((2*NBX+1) * (2*NBY+1)) - mean; // Should be divided (float)((2*NBX+1) * (2*NBY+1));

                if ((in_minus_mean > 0.0f) && // pixel value is larger than mean
                    (mean > 0.0f) &&          // mean is larger than zero (no bad pixels)
                    (in[(line0 + line)*COLS+col] > 0) && // pixel is not bad pixel and is above 0
                    (in_minus_mean * in_minus_mean > var * threshold)) {
                       // Save line, column and photon count in output table
                       out[strong_id0+strong_id].line = line;
                       out[strong_id0+strong_id].col = col;
                       out[strong_id0+strong_id].photons = in[(line0 + line)*COLS+col];
                       strong_id = (strong_id + 1 ) % MAX_STRONG;
                    }

                // Updated value of sum and sum2
                // For last column - these need not to be calculated
                if (col < COLS - NBX - 1) {
                   sum += sum_vert[col + NBX + 1] - sum_vert[col - NBX];
                   sum2 += sum2_vert[col + NBX + 1] - sum2_vert[col - NBX];

                }
            }
            // Shift sum_vert and sum2_vert by one line
            if (line < LINES - NBY - 1) {
                for (int col = 0; col < COLS; col++) {
                    int64_t tmp_sum  = (int64_t)in[(line0+line+NBY+1) * COLS + col] + (int64_t)in[(line0 + line-NBY) * COLS + col];
                    int64_t tmp_diff = (int64_t)in[(line0+line+NBY+1) * COLS + col] - (int64_t)in[(line0 + line-NBY) * COLS + col];
                    sum_vert[col]  += tmp_diff;
                    sum2_vert[col] += tmp_sum * tmp_diff; // in[(line0+line+NBY+1) * MODULE_COLS + col]^2 - in[(line0 + line-NBY) * MODULE_COLS + col]^2
                }
            }
        }
        // Mark, where useful data and in output table
        out[strong_id0+strong_id].line = -1;
        out[strong_id0+strong_id].col = -1;
        out[strong_id0+strong_id].photons = strong_id;
   }
}

int16_t *gpu_data16;
int32_t *gpu_data32;
strong_pixel *gpu_out;

int setup_gpu(int device) {
    std::cout << "GPU: Setup start of device " << device << std::endl;
    cudaSetDevice(device);

    cudaError_t err = cudaHostRegister(ib_buffer, ib_buffer_size, cudaHostRegisterPortable);
    if (err != cudaSuccess) {
         std::cout << "GPU: Register error " << cudaGetErrorString(err) << " addr " << ib_buffer << " size " << ib_buffer_size << std::endl;
         return 1;
    }

    size_t gpu_data16_size = NCUDA_STREAMS * NFRAMES_PER_STREAM * FRAME_SIZE;
    err = cudaMalloc((void **) &gpu_data16, gpu_data16_size);
    if (err != cudaSuccess) {
         std::cout << "GPU: Mem alloc. error (data) " <<  gpu_data16_size / 1024 / 1024 << std::endl;
         return 1;
    }

    err = cudaMalloc((void **) &gpu_out, NCUDA_STREAMS * NFRAMES_PER_STREAM * 2 * MAX_STRONG * sizeof(strong_pixel)); // frame is divided into 2 vertical slices
    if (err != cudaSuccess) {
         std::cout << "GPU: Mem alloc. error (output)" << std::endl;
         return 1;
    }

    for (int i = 0; i < NCUDA_STREAMS; i++) {
        err = cudaStreamCreate(&stream[i]);
        if (err != cudaSuccess) {
            std::cout << "GPU: Stream create error" << std::endl;
            return 1;
        }
    }
    std::cout << "GPU: setup done" << std::endl;

    return 0;
}

int close_gpu() {
    cudaFree(gpu_out);
    cudaFree(gpu_data16);
    cudaError_t err = cudaHostUnregister(ib_buffer);
    for (int i = 0; i < NCUDA_STREAMS; i++)
        err = cudaStreamDestroy(stream[i]);
    return 0;
}

// CPU part
// (recursion will not fit well to GPU)
// Constructing spot from strong pixels

struct spot_t {
    double x,y,z;
    uint16_t module; // number of module - it is impossible for spot to belong to more than one module
    uint64_t photons; // total photon count
    uint64_t pixels; // number of pixels
    uint64_t depth; // on how many frames the spot is present
};

// Adds two spot measurements
void merge_spots(spot_t &spot1, const spot_t &spot2) {
    if (spot2.photons > 0) {
        spot1.x = spot1.x + spot2.x;
        spot1.y = spot1.y + spot2.y;
        spot1.z = spot1.z + spot2.z;
        spot1.photons = spot1.photons + spot2.photons;
        spot1.pixels = spot1.pixels + spot2.pixels;
    }
}

// If spots come from two different frames, depth needs to be incremented
void merge_spots_new_frame(spot_t &spot1, const spot_t &spot2) {
    if (spot2.photons > 0) {
        spot1.x = spot1.x + spot2.x;
        spot1.y = spot1.y + spot2.y;
        spot1.z = spot1.z + spot2.z;
        spot1.photons = spot1.photons + spot2.photons;
        spot1.pixels = spot1.pixels + spot2.pixels;
        spot1.depth = spot1.depth + spot2.depth + 1;
    }
}


spot_t add_pixel(std::map<std::pair<uint16_t, uint16_t>, uint64_t> *dictionary, uint64_t frame, uint16_t module, uint16_t line, uint16_t col) {
    spot_t ret_value;
    std::map<std::pair<uint16_t, uint16_t>, uint64_t>::iterator iterator = dictionary[frame*NFRAMES_PER_STREAM+module].find(std::pair<uint16_t, uint16_t>(line,col));
    if (iterator != dictionary[frame*NFRAMES_PER_STREAM+module].end()) {
        uint64_t photons = iterator->second;
        ret_value.module = module;
        ret_value.x = line * (double)photons; // position is weighted by number of photon counts
        ret_value.y = col * (double)photons;
        ret_value.z = frame * (double)photons;
        ret_value.photons = photons;
        ret_value.pixels = 1;
        ret_value.depth = 0;

        dictionary[frame*NFRAMES_PER_STREAM+module].erase(iterator); // Remove strong pixel from the dictionary, so it is not processed again

        // Recursively analyze all neighbors 
        merge_spots(ret_value, add_pixel(dictionary, frame, module, line+1, col-1));
        merge_spots(ret_value, add_pixel(dictionary, frame, module, line+1, col));
        merge_spots(ret_value, add_pixel(dictionary, frame, module, line+1, col+1));
        merge_spots(ret_value, add_pixel(dictionary, frame, module, line-1, col-1));
        merge_spots(ret_value, add_pixel(dictionary, frame, module, line-1, col));
        merge_spots(ret_value, add_pixel(dictionary, frame, module, line-1, col+1));
        merge_spots(ret_value, add_pixel(dictionary, frame, module, line, col+1));
        merge_spots(ret_value, add_pixel(dictionary, frame, module, line, col-1));
        // Recursively check if spot in next frame is also strong
        if (frame + 1 < NFRAMES_PER_STREAM) merge_spots_new_frame(ret_value, add_pixel(dictionary, frame + 1, module, line, col));
    } else {
        ret_value.photons = 0;
        ret_value.pixels = 0;
    }
    return ret_value;

}

// Gather spots
void process_frames(std::map<std::pair<uint16_t, uint16_t>, uint64_t> *dictionary, std::vector<spot_t> &spots) {
   for (int i = 0; i < NFRAMES_PER_STREAM*2; i++) {
      std::map<std::pair<uint16_t, uint16_t>, uint64_t>::iterator iterator = dictionary[i].begin();
      while (iterator != dictionary[i].end()) {
          spot_t spot = add_pixel(dictionary, i / 2, i % 2, iterator->first.first, iterator->first.second);

          // Apply pixel count cut-off and cut-off of number of frames, which spot can span 
          // (spots present in most frames, are likely to be either bad pixels or in spindle axis)
          if ((spot.pixels > 3) && (spot.depth < 100)) spots.push_back(spot);
          iterator = dictionary[i].begin(); // Get first unprocessed spot in this frame
      }
   }
}

void analyze_spots(strong_pixel *host_out, std::vector<spot_t> &spots) {
    // key is location of strong pixel - value is number of photons
    std::map<std::pair<uint16_t, uint16_t>, uint64_t> dictionary[NFRAMES_PER_STREAM*2]; 

    // Transfer strong pixels into dictionary
    for (size_t i = 0; i < NFRAMES_PER_STREAM*2; i++) {
        size_t addr = i * MAX_STRONG;
        int k = 0;
        while ((k < MAX_STRONG) && (host_out[addr + k].col >= 0)) {
              std::pair<uint16_t, uint16_t> key = std::pair<uint16_t, uint16_t>(host_out[addr + k].line,host_out[addr + k].col);
              (dictionary[i])[key] = host_out[addr + k].photons;
              k++;
        }
    }
    process_frames(dictionary,spots);
}


void *run_gpu_thread(void *in_threadarg) {
    ThreadArg *arg = (ThreadArg *) in_threadarg;

    // Account for leftover
    size_t total_chunks = experiment_settings.nframes_to_write / NFRAMES_PER_STREAM;
    if (experiment_settings.nframes_to_write - total_chunks * NFRAMES_PER_STREAM > 0)
           total_chunks++;

    size_t gpu_slice = arg->ThreadID;

    cudaEvent_t event_mem_copied;
    cudaEventCreate (&event_mem_copied);

    strong_pixel *host_out = (strong_pixel *) calloc(NFRAMES_PER_STREAM * 2 * MAX_STRONG, sizeof(strong_pixel));

    for (size_t chunk = gpu_slice;
         chunk < total_chunks;
         chunk += NCUDA_STREAMS) {

         size_t ib_slice = chunk % (NCUDA_STREAMS*CUDA_TO_IB_BUFFER);

//         size_t frame0 = ib_slice * NFRAMES_PER_STREAM;
         size_t frames = experiment_settings.nframes_to_write - gpu_slice * NFRAMES_PER_STREAM;
         if (frames > NFRAMES_PER_STREAM) frames = NFRAMES_PER_STREAM;

         pthread_mutex_lock(writer_threads_done_mutex+ib_slice);
         // Wait till everyone is done
         while (writer_threads_done[ib_slice] > 0)
             pthread_cond_wait(writer_threads_done_cond+ib_slice, 
                               writer_threads_done_mutex+ib_slice);
         // Restore full values and continue
         writer_threads_done[ib_slice] = receiver_settings.compression_threads;
         pthread_mutex_unlock(writer_threads_done_mutex+ib_slice);

         // Here all writting is done, but it is guarranteed not be overwritten


         // Copy frames to GPU memory
         cudaError_t err;
         err = cudaMemcpyAsync(gpu_data16 + gpu_slice * NFRAMES_PER_STREAM * FRAME_SIZE, 
               ib_buffer + ib_slice * FRAME_SIZE,
               frames * FRAME_SIZE,
               cudaMemcpyHostToDevice, stream[gpu_slice]);
         if (err != cudaSuccess) {
             std::cout << "GPU: memory copy error" << std::endl;
             pthread_exit(0);
         }

         cudaEventRecord (event_mem_copied, stream[gpu_slice]);

         // Start GPU kernel
         find_spots_colspot<<<NFRAMES_PER_STREAM * 2 / 32, 32, 0, stream[gpu_slice]>>> 
                 (gpu_data16 + gpu_slice * NFRAMES_PER_STREAM * FRAME_SIZE, 
                  gpu_out + gpu_slice * NFRAMES_PER_STREAM * 2 * MAX_STRONG, 
                  experiment_settings.strong_pixel_value, frames * 2);

         // After data are copied, one can release buffer
         err = cudaEventSynchronize(event_mem_copied);
         if (err != cudaSuccess) {
             std::cout << "GPU: memory copy error" << std::endl;
             pthread_exit(0);
         }

         // Broadcast to everyone waiting, that buffer can be overwritten
         pthread_mutex_lock(cuda_stream_ready_mutex+ib_slice);
         cuda_stream_ready[ib_slice] = receiver_settings.compression_threads;
         pthread_cond_broadcast(cuda_stream_ready_cond+ib_slice);
         pthread_mutex_unlock(cuda_stream_ready_mutex+ib_slice);

         err = cudaMemcpyAsync(host_out, 
                         gpu_out + gpu_slice * NFRAMES_PER_STREAM * 2 * MAX_STRONG,
                         NFRAMES_PER_STREAM * 2 * MAX_STRONG * sizeof(strong_pixel),
                         cudaMemcpyDeviceToHost, stream[gpu_slice]);

         // Ensure kernel has finished
         err = cudaStreamSynchronize(stream[gpu_slice]);
         if (err != cudaSuccess) {
             std::cout << "GPU: execution error" << std::endl;
             pthread_exit(0);
         }


         // Analyze results to find spots
    }

    cudaEventDestroy (event_mem_copied);
    pthread_exit(0);
}
