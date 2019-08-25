#include "MovieDecoder.h"

#include "Audio/AudioData.h"
#include "Base/ByteStream.h"
#include "Base/Endian.h"
#include "Base/Finally.h"
#include "ChunkedStreamFileUtils.h"
#include <memory>

BEGIN_NAMESPACE(MovieDecoder)

// Header for video data in a movie file
struct VideoStreamHeader {
    CSFFourCID      id;             // Should say 'CVID'
    uint32_t        height;         // Height of the movie in pixels
    uint32_t        width;          // Width of the movie in pixels
    uint32_t        fps;            // Framerate of the movie
    uint32_t        numFrames;      // Number of frames in the movie

    void convertBigToHostEndian() noexcept {
        Endian::convertBigToHost(height);
        Endian::convertBigToHost(width);
        Endian::convertBigToHost(fps);
        Endian::convertBigToHost(numFrames);
    }
};

//----------------------------------------------------------------------------------------------------------------------
// Header for a frame of video data in a movie file
//----------------------------------------------------------------------------------------------------------------------
struct VideoFrameHeader {
    // Size of the entire frame data, minus this 32-bit field
    uint32_t frameSize;
    
    // If bit 0 is set then it means the codebooks for each strip in the frame uses the codebook for the previous
    // strip in the frame. If there is no previous strip in the frame, which will always be the case for the videos
    // we are decoding ('numStrips' is always '1') then it means to use the codebook from the previous frame.
    //
    // Note: bits 1-7 are unused, and should be always cleared!
    uint8_t flags;

    // Size of the CVID data in the frame as a 24-bit big endian unsigned.
    // Storing as bytes because there is no standard 24-bit unsigned type in C++...
    uint8_t cvidDataLength[3];

    uint16_t width;         // Width of the video data
    uint16_t height;        // Height of the video data
    uint16_t numStrips;     // Number of coded strips in the CPAK video, I only support '1'
    uint16_t _unknown;

    void convertBigToHostEndian() noexcept {
        Endian::convertBigToHost(frameSize);
        Endian::convertBigToHost(width);
        Endian::convertBigToHost(height);
        Endian::convertBigToHost(numStrips);
    }
};

// Strip types
static constexpr uint16_t STRIP_TYPE_KEY_FRAME      = 0x1000;     // This video strip is a keyframe and fully specified
static constexpr uint16_t STRIP_TYPE_DELTA_FRAME    = 0x1100;     // This video strip is a frame that only receives partial updates from the previous frame

//----------------------------------------------------------------------------------------------------------------------
// Header for a strip of video data in the CPAK codec.
// CPAK allows many of these per frame, but we only need to support 1 for decoding these movies.
//----------------------------------------------------------------------------------------------------------------------
struct VideoStripHeader {
    uint16_t    _unknown1;
    uint16_t    _unknown2;
    uint16_t    stripType;      // Either keyframe or delta frame
    uint16_t    stripSize;      // Size of the strip data
    uint16_t    topY;           // Rect defining what part of the video this strip encodes
    uint16_t    leftX;
    uint16_t    height;
    uint16_t    width;

    void convertBigToHostEndian() noexcept {
        Endian::convertBigToHost(stripType);
        Endian::convertBigToHost(stripSize);
        Endian::convertBigToHost(topY);
        Endian::convertBigToHost(leftX);
        Endian::convertBigToHost(height);
        Endian::convertBigToHost(width);
    }
};

//----------------------------------------------------------------------------------------------------------------------
// Header for a CVID chunk in the CPAK codec.
//----------------------------------------------------------------------------------------------------------------------
struct CVIDChunkHeader {
    uint16_t    chunkType;
    uint16_t    chunkSize;

    void convertBigToHostEndian() noexcept {
        Endian::convertBigToHost(chunkType);
        Endian::convertBigToHost(chunkSize);
    }
};

// CVID Chunk types: key frames
static constexpr uint16_t CVID_KF_V4_CODEBOOK_12_BIT    = 0x2000;
static constexpr uint16_t CVID_KF_V1_CODEBOOK_12_BIT    = 0x2200;
static constexpr uint16_t CVID_KF_V4_CODEBOOK_8_BIT     = 0x2400;
static constexpr uint16_t CVID_KF_V1_CODEBOOK_8_BIT     = 0x2600;
static constexpr uint16_t CVID_KF_VECTORS               = 0x3000;
static constexpr uint16_t CVID_KF_VECTORS_V1_ONLY       = 0x3200;

// CVID Chunk types: delta frames (partially updated frames)
static constexpr uint16_t CVID_DF_V4_CODEBOOK_12_BIT    = 0x2100;
static constexpr uint16_t CVID_DF_V1_CODEBOOK_12_BIT    = 0x2300;
static constexpr uint16_t CVID_DF_V4_CODEBOOK_8_BIT     = 0x2500;
static constexpr uint16_t CVID_DF_V1_CODEBOOK_8_BIT     = 0x2700;
static constexpr uint16_t CVID_DF_VECTORS               = 0x3100;

//----------------------------------------------------------------------------------------------------------------------
// Header for audio data in a movie file
//----------------------------------------------------------------------------------------------------------------------
struct AudioStreamHeader {
    uint32_t    _unknown1;
    uint32_t    _unknown2;
    uint32_t    _unknown3;
    uint32_t    _unknown4;
    uint32_t    bitDepth;
    uint32_t    sampleRate;
    uint32_t    numChannels;
    uint32_t    _unknown5;
    uint32_t    _unknown6;
    uint32_t    audioDataSize;

    void convertBigToHostEndian() noexcept {
        Endian::convertBigToHost(bitDepth);
        Endian::convertBigToHost(sampleRate);
        Endian::convertBigToHost(numChannels);
        Endian::convertBigToHost(audioDataSize);
    }
};

//----------------------------------------------------------------------------------------------------------------------
// Attempt to read a CVID chunk in the data for the video frame strip
//----------------------------------------------------------------------------------------------------------------------
static void readCVIDChunk(VideoDecoderState& decoderState, ByteStream& stream) THROWS {
    // Read the chunk header details
    CVIDChunkHeader chunkHeader;
    stream.read(chunkHeader);
    chunkHeader.convertBigToHostEndian();

    // See what type of chunk we are dealing with
    
    // TODO...

    {
        // Unknown type of chunk, skip over it
        stream.consume(chunkHeader.chunkSize);
    }
}

bool initVideoDecoder(
    const std::byte* const pStreamFileData,
    const uint32_t streamFileSize,
    VideoDecoderState& decoderState
) noexcept {
    ASSERT(pStreamFileData || streamFileSize <= 0);

    // Default initialize the decoder state initially
    std::memset(&decoderState, 0, sizeof(VideoDecoderState));

    // Grab the video stream data and abort if that fails
    bool bInitializedSuccessfully = false;
    const bool bGotVideoStreamData = ChunkedStreamFileUtils::getSubStreamData(
        pStreamFileData,
        streamFileSize,
        CSFFourCID::make("FILM"),
        decoderState.pMovieData,
        decoderState.movieDataSize
    );

    auto cleanupOnFailure = finally([&]() noexcept {
        if (!bInitializedSuccessfully) {
            shutdownVideoDecoder(decoderState);
        }
    });

    if (!bGotVideoStreamData)
        return false;

    // There should be at least the header and more data following it.
    // Grab the header and verify that it looks as we expect:
    if (decoderState.movieDataSize <= sizeof(VideoStreamHeader))
        return false;

    VideoStreamHeader header;
    std::memcpy(&header, decoderState.pMovieData, sizeof(VideoStreamHeader));
    header.convertBigToHostEndian();

    if (header.id != CSFFourCID::make("cvid") && header.id != CSFFourCID::make("CVID"))
        return false;
    
    if (header.width != VIDEO_WIDTH || header.height != VIDEO_HEIGHT)
        return false;

    // Note: disabled this check because 'logic.cine' appears to have garbage in this field or at least something I don't know
    // how to interpret. Both videos used in 3DO Doom should be 12 FPS anyway so I will just make that assumption...
    #if 0
    if (header.fps != VIDEO_FPS)
        return false;
    #endif
    
    decoderState.curMovieDataOffset += sizeof(VideoStreamHeader);

    // All good if we get to here - fill in the rest of the details!
    decoderState.totalFrames = header.numFrames;
    bInitializedSuccessfully = true;
    return true;
}

void shutdownVideoDecoder(VideoDecoderState& decoderState) noexcept {
    delete[] decoderState.pMovieData;
    decoderState.pMovieData = nullptr;
}

bool readNextVideoFrame(VideoDecoderState& decoderState) noexcept {
    // Sanity checks: decoder must have been initialized
    ASSERT(decoderState.pMovieData);
    ASSERT(decoderState.movieDataSize > 0);

    // Can't read if we are at the end of the data
    const bool bAtMovieEnd = (
        (decoderState.curMovieDataOffset >= decoderState.movieDataSize) ||
        (decoderState.frameNum >= decoderState.totalFrames)
    );

    if (bAtMovieEnd)
        return false;
    
    // Start reading
    ByteStream frameData(
        decoderState.pMovieData + decoderState.curMovieDataOffset,
        decoderState.movieDataSize - decoderState.curMovieDataOffset
    );

    try {
        // Read the frame header and verify it is correct
        VideoFrameHeader frameHeader;
        frameData.read(frameHeader);
        frameHeader.convertBigToHostEndian();

        const uint32_t frameSizeRemaining = (
            frameHeader.frameSize
            + sizeof(uint32_t)              // Have to add this on because the frame size read does not include the 'size' field!
            - sizeof(VideoFrameHeader)
        );

        const bool bInvalidFrameHeader = (
            (frameSizeRemaining > frameData.getNumBytesLeft()) ||                           // Bad data length?
            (frameHeader.width != VIDEO_WIDTH || frameHeader.height != VIDEO_HEIGHT) ||     // Unexpected size?
            (frameHeader.numStrips != 1)                                                    // Allow just 1 strip per frame
        );

        if (bInvalidFrameHeader)
            return false;
        
        // Read the header for the single strip that we expect to be in the frame and validate
        VideoStripHeader stripHeader;
        frameData.read(stripHeader);
        stripHeader.convertBigToHostEndian();

        const bool bInvalidStripHeader = (
            (stripHeader.stripType != STRIP_TYPE_KEY_FRAME && stripHeader.stripType != STRIP_TYPE_DELTA_FRAME) ||
            (stripHeader.topY != 0) ||
            (stripHeader.leftX != 0) ||
            (stripHeader.height != VIDEO_HEIGHT) ||
            (stripHeader.width != VIDEO_WIDTH)
        );

        if (bInvalidStripHeader)
            return false;
        
        // Read all of the CVID chunks that follow
        ByteStream stripData(frameData.getCurData(), stripHeader.stripSize);

        while (frameData.hasBytesLeft() > sizeof(CVIDChunkHeader)) {
            readCVIDChunk(decoderState, frameData);
        }

        // Update decoder state to point to the next frame
        ++decoderState.frameNum;
        decoderState.curMovieDataOffset += frameHeader.frameSize + 4;

        // Frame decode succeeded
        return true;
    }
    catch (...) {
        // Failed to decode!
        return false;
    }
}

bool decodeMovieAudio(
    const std::byte* const pStreamFileData,
    const uint32_t streamFileSize,
    AudioData& audioDataOut
) noexcept {
    ASSERT(pStreamFileData || streamFileSize <= 0);

    // Default initialize the output until we are successful
    audioDataOut = {};

    // Firstly try to grab the audio stream data from the header
    std::byte* pAudioStreamData = nullptr;
    uint32_t audioStreamDataSize = 0;

    auto cleanupAudioStreamData = finally([&]() noexcept {
        delete[] pAudioStreamData;
    });

    const bool bGotAudioStreamData = ChunkedStreamFileUtils::getSubStreamData(
        pStreamFileData,
        streamFileSize,
        CSFFourCID::make("SNDS"),
        pAudioStreamData,
        audioStreamDataSize
    );

    if (!bGotAudioStreamData)
        return false;

    // Read the header
    if (audioStreamDataSize <= sizeof(AudioStreamHeader))
        return false;

    AudioStreamHeader header;
    std::memcpy(&header, pAudioStreamData, sizeof(AudioStreamHeader));
    header.convertBigToHostEndian();

    // Sanity check the header and make sure it has a supported format
    const bool bInvalidHeader = (
        (header.audioDataSize + sizeof(AudioStreamHeader) > audioStreamDataSize) ||
        (header.numChannels != 1 && header.numChannels != 2) ||
        (header.bitDepth != 8 && header.bitDepth != 16) ||
        (header.sampleRate <= 0)
    );

    if (bInvalidHeader)
        return false;
    
    // The rest is easy, just fill in the audio data struct, copy the audio data and return 'true' for success
    const uint32_t bytesPerSample = header.bitDepth / 8;

    audioDataOut.allocBuffer(header.audioDataSize);
    audioDataOut.bufferSize = header.audioDataSize;
    audioDataOut.numSamples = (header.audioDataSize / bytesPerSample) / header.numChannels;
    audioDataOut.sampleRate = header.sampleRate;
    audioDataOut.numChannels = header.numChannels;
    audioDataOut.bitDepth = header.bitDepth;

    std::memcpy(audioDataOut.pBuffer, pAudioStreamData + sizeof(AudioStreamHeader), header.audioDataSize);
    return true;
}

END_NAMESPACE(MovieDecoder)