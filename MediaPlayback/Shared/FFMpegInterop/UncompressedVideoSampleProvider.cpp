//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "pch.h"

#ifndef NO_FFMPEG

#include "UncompressedVideoSampleProvider.h"

extern "C"
{
#include <libavutil/imgutils.h>
}


using namespace FFmpegInterop;
using namespace ABI::Windows::Storage::Streams;

_Use_decl_annotations_
UncompressedVideoSampleProvider::UncompressedVideoSampleProvider(
    std::weak_ptr<FFmpegReader> reader,
    AVFormatContext* avFormatCtx,
    AVCodecContext* avCodecCtx)
    : UncompressedSampleProvider(reader, avFormatCtx, avCodecCtx)
    , m_pSwsCtx(nullptr)
{
    for (int i = 0; i < 4; i++)
    {
        m_rgVideoBufferLineSize[i] = 0;
        m_rgVideoBufferData[i] = nullptr;
    }
}

_Use_decl_annotations_
UncompressedVideoSampleProvider::~UncompressedVideoSampleProvider()
{
    if (m_pAvFrame)
    {
        av_frame_free(&m_pAvFrame);
    }

    if (m_rgVideoBufferData)
    {
        av_freep(m_rgVideoBufferData);
    }
}

_Use_decl_annotations_
HRESULT UncompressedVideoSampleProvider::AllocateResources()
{
    HRESULT hr = S_OK;
    hr = UncompressedSampleProvider::AllocateResources();
    if (SUCCEEDED(hr))
    {
        // Setup software scaler to convert any decoder pixel format (e.g. YUV420P) to NV12 that is supported in Windows & Windows Phone MediaElement
        m_pSwsCtx = sws_getContext(
            m_pAvCodecCtx->width,
            m_pAvCodecCtx->height,
            m_pAvCodecCtx->pix_fmt,
            m_pAvCodecCtx->width,
            m_pAvCodecCtx->height,
            AV_PIX_FMT_NV12,
            SWS_BICUBIC,
            NULL,
            NULL,
            NULL);

        if (m_pSwsCtx == nullptr)
        {
            hr = E_OUTOFMEMORY;
        }
    }

    if (SUCCEEDED(hr))
    {
        m_pAvFrame = av_frame_alloc();
        if (m_pAvFrame == nullptr)
        {
            hr = E_OUTOFMEMORY;
        }
    }

    if (SUCCEEDED(hr))
    {
        if (av_image_alloc(m_rgVideoBufferData, m_rgVideoBufferLineSize, m_pAvCodecCtx->width, m_pAvCodecCtx->height, AV_PIX_FMT_NV12, 1) < 0)
        {
            hr = E_FAIL;
        }
    }

    return hr;
}

_Use_decl_annotations_
HRESULT UncompressedVideoSampleProvider::DecodeAVPacket(IDataWriter* dataWriter, AVPacket* avPacket, int64_t& framePts, int64_t& frameDuration)
{
    HRESULT hr = S_OK;
    hr = UncompressedSampleProvider::DecodeAVPacket(dataWriter, avPacket, framePts, frameDuration);

    // Don't set a timestamp on S_FALSE
    if (hr == S_OK)
    {
        // Try to get the best effort timestamp for the frame.
        framePts = av_frame_get_best_effort_timestamp(m_pAvFrame);
    }

    return hr;
}

_Use_decl_annotations_
HRESULT UncompressedVideoSampleProvider::WriteAVPacketToStream(IDataWriter* dataWriter, AVPacket* avPacket)
{
    // Convert decoded video pixel format to NV12 using FFmpeg software scaler
    if (sws_scale(m_pSwsCtx, (const uint8_t **)(m_pAvFrame->data), m_pAvFrame->linesize, 0, m_pAvCodecCtx->height, m_rgVideoBufferData, m_rgVideoBufferLineSize) < 0)
    {
        return E_FAIL;
    }

    // YBuffer
    dataWriter->WriteBytes(m_rgVideoBufferLineSize[0] * m_pAvCodecCtx->height, m_rgVideoBufferData[0]);
    // UVBuffer
    dataWriter->WriteBytes(m_rgVideoBufferLineSize[1] * m_pAvCodecCtx->height / 2, m_rgVideoBufferData[1]);
    av_frame_unref(m_pAvFrame);
    av_frame_free(&m_pAvFrame);

    return S_OK;
}

#endif // NO_FFMPEG