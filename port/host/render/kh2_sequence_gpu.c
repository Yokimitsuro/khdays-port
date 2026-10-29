/* The native sequence image compute pass; existing UI images and the existing atlas store. */
#include "kh2_sequence_gpu.h"
#include "gpu_hd.h"
#include "generated/kh2_sequence_cs.h"

static SDL_GPUDevice *device;
static SDL_GPUComputePipeline *pipeline;
static SDL_GPUBuffer *commands;
static SDL_GPUTransferBuffer *upload;

void khdays_sequence_gpu_close(void)
{
    if (device) {
        if (pipeline)
            SDL_ReleaseGPUComputePipeline(device, pipeline);
        if (commands)
            SDL_ReleaseGPUBuffer(device, commands);
        if (upload)
            SDL_ReleaseGPUTransferBuffer(device, upload);
    }
    device = NULL;
    pipeline = NULL;
    commands = NULL;
    upload = NULL;
}

int khdays_sequence_gpu_open(SDL_GPUDevice *d)
{
    if (device == d && pipeline && commands && upload)
        return 1;
    khdays_sequence_gpu_close();
    device = d;
    SDL_GPUComputePipelineCreateInfo info;
    SDL_zero(info);
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
    if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
        info.code = kh2_sequence_cs_dxil;
        info.code_size = kh2_sequence_cs_dxil_size;
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
    } else if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.code = kh2_sequence_cs_spirv;
        info.code_size = kh2_sequence_cs_spirv_size;
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    } else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
        info.code = kh2_sequence_cs_msl;
        info.code_size = kh2_sequence_cs_msl_size - 1;
        info.format = SDL_GPU_SHADERFORMAT_MSL;
    } else
        return SDL_SetError("no supported shader format for native sequences");
    info.entrypoint = KH2_SEQUENCE_CS_ENTRY;
    info.num_readonly_storage_buffers = 1;
    info.num_readwrite_storage_buffers = 1;
    info.threadcount_x = info.threadcount_y = 8;
    info.threadcount_z = 1;
    pipeline = SDL_CreateGPUComputePipeline(device, &info);
    SDL_GPUBufferCreateInfo buffer_info;
    SDL_zero(buffer_info);
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    buffer_info.size = sizeof(KhdaysSequenceImage);
    commands = SDL_CreateGPUBuffer(device, &buffer_info);
    SDL_GPUTransferBufferCreateInfo transfer_info;
    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = sizeof(KhdaysSequenceImage);
    upload = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    return pipeline && commands && upload;
}

int khdays_sequence_gpu_draw(SDL_GPUDevice *d, SDL_GPUCommandBuffer *cmd, const KhdaysSequenceImage *image)
{
    if (image->words[KSI_COUNT] == 0 && !image->words[KSI_CLEAR])
        return 1;
    if (image->words[KSI_COUNT] > KSI_MAX_QUADS || !khdays_sequence_gpu_open(d))
        return 0;
    const Uint32 bytes = (KSI_HEADER + image->words[KSI_COUNT] * KSI_QUAD_WORDS) * 4u;
    void *mapped = SDL_MapGPUTransferBuffer(device, upload, true);
    if (!mapped)
        return 0;
    SDL_memcpy(mapped, image, bytes);
    SDL_UnmapGPUTransferBuffer(device, upload);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    const SDL_GPUTransferBufferLocation from = {upload, 0};
    const SDL_GPUBufferRegion to = {commands, 0, bytes};
    SDL_UploadToGPUBuffer(copy, &from, &to, true);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUStorageBufferReadWriteBinding target;
    SDL_zero(target);
    target.buffer = khdays_gpu_hd_buffer();
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cmd, NULL, 0, &target, 1);
    SDL_BindGPUComputePipeline(pass, pipeline);
    SDL_BindGPUComputeStorageBuffers(pass, 0, &commands, 1);
    SDL_DispatchGPUCompute(pass, (image->words[KSI_WIDTH] + 7u) / 8u, (image->words[KSI_HEIGHT] + 7u) / 8u, 1);
    SDL_EndGPUComputePass(pass);
    return 1;
}
