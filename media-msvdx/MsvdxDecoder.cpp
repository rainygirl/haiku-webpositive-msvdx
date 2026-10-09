/*
 * msvdx_h264: a Media Kit decoder add-on that decodes H.264 on the video
 * decoder of the Intel GMA500 (Poulsbo), as found in the Sony VAIO P.
 *
 * It registers the same format description the ffmpeg reader gives H.264
 * tracks, and the Media Kit searches user add-on directories before the
 * system's, so installed under ~/config/non-packaged/add-ons it is the
 * decoder BMediaTrack picks for H.264 -- in WebPositive (HTML5 video), in
 * MediaPlayer, in anything that reads video through the Media Kit.
 *
 * The Media Kit does not fall back to another decoder when the one it picked
 * fails, so this one does it itself: when there is no firmware, no GMA500, the
 * hardware is taken (R Chromium or R Television playing), or the stream is one
 * this path does not handle (interlaced, not 4:2:0, larger than 1920x1088), it
 * loads the system's ffmpeg plugin and hands the whole track to its decoder,
 * the chunk that failed included.
 *
 * Pictures come out of the hardware as NV12 and are converted with libswscale
 * to whatever raw format the caller negotiates (B_RGB32 by default, which is
 * what WebKit's MediaPlayerPrivateHaiku and MediaPlayer ask for).
 */
#include <Architecture.h>
#include <FindDirectory.h>
#include <MediaFormats.h>
#include <Path.h>
#include <image.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <deque>
#include <new>
#include <vector>

extern "C" {
#include <libavcodec/codec_id.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include "DecoderPlugin.h"
#include "rtv_msvdx.h"


namespace {

// MSVDX_MEDIA_DEBUG=1: say on stderr what the decoder decides and why.
bool
Debug()
{
	static int on = -1;
	if (on < 0) {
		const char* env = getenv("MSVDX_MEDIA_DEBUG");
		on = env != NULL && strcmp(env, "0") != 0;
	}
	return on;
}
#define TRACE(...) do { if (Debug()) fprintf(stderr, "msvdx_h264: " __VA_ARGS__); } while (0)

struct Picture {
	std::vector<uint8> luma, chroma;
	int stride, width, height;
	int64 pts;
};


class MsvdxDecoder : public Decoder, private ChunkProvider {
public:
	MsvdxDecoder();
	~MsvdxDecoder() override;

	void GetCodecInfo(media_codec_info* info) override;
	status_t Setup(media_format* ioEncodedFormat, const void* infoBuffer,
		size_t infoSize) override;
	status_t NegotiateOutputFormat(media_format* ioDecodedFormat) override;
	status_t SeekedTo(int64 frame, bigtime_t time) override;
	status_t Decode(void* buffer, int64* frameCount, media_header* header,
		media_decode_info* info) override;

private:
	// ChunkProvider, for the ffmpeg decoder we hand a track to.
	status_t GetNextChunk(const void** chunk, size_t* size,
		media_header* header) override;

	// Decoder owns and deletes its ChunkProvider (also when replacing it).
	// The fallback must own a forwarding object, not the outer decoder itself.
	class FallbackChunks : public ChunkProvider {
	public:
		explicit FallbackChunks(MsvdxDecoder* owner) : fOwner(owner) {}
		status_t GetNextChunk(const void** chunk, size_t* size,
			media_header* header) override
		{ return fOwner->GetNextChunk(chunk, size, header); }
	private:
		MsvdxDecoder* fOwner; // borrowed; owns this provider through fFfmpeg
	};

	static void OnPicture(void* opaque, const rtv_msvdx_picture* picture);
	bool ParseAvcC(const uint8* data, size_t size);
	void ToAnnexB(const uint8* data, size_t size);
	status_t UseFfmpeg(const void* replay, size_t replaySize,
		const media_header* replayHeader);
	status_t Convert(const Picture& picture, void* buffer);

	media_format fEncodedFormat;
	std::vector<uint8> fInfo;
	media_format fOutputFormat;
	bool fOutputNegotiated;
	int fWidth, fHeight;

	rtv_msvdx* fHardware;
	int fNalLengthSize;		// 0: Annex B already
	std::vector<uint8> fAnnexB;
	std::deque<Picture> fPictures;
	bool fDrained;

	SwsContext* fScaler;
	int fScalerWidth, fScalerHeight;
	color_space fScalerSpace;

	// the fallback
	image_id fFfmpegImage;
	MediaPlugin* fFfmpegPlugin;
	Decoder* fFfmpeg;
	std::vector<uint8> fReplay;
	media_header fReplayHeader;
	bool fReplayPending;
};


MsvdxDecoder::MsvdxDecoder()
	:
	fOutputNegotiated(false),
	fWidth(0),
	fHeight(0),
	fHardware(NULL),
	fNalLengthSize(0),
	fDrained(false),
	fScaler(NULL),
	fScalerWidth(0),
	fScalerHeight(0),
	fScalerSpace(B_NO_COLOR_SPACE),
	fFfmpegImage(-1),
	fFfmpegPlugin(NULL),
	fFfmpeg(NULL),
	fReplayPending(false)
{
	memset(&fReplayHeader, 0, sizeof(fReplayHeader));
}


MsvdxDecoder::~MsvdxDecoder()
{
	rtv_msvdx_destroy(fHardware);
	delete fFfmpeg;
	delete fFfmpegPlugin;
	if (fFfmpegImage >= 0)
		unload_add_on(fFfmpegImage);
	sws_freeContext(fScaler);
}


void
MsvdxDecoder::GetCodecInfo(media_codec_info* info)
{
	if (fFfmpeg != NULL) {
		fFfmpeg->GetCodecInfo(info);
		return;
	}
	strlcpy(info->short_name, "h264-msvdx", sizeof(info->short_name));
	strlcpy(info->pretty_name, "H.264 on the GMA500 video decoder",
		sizeof(info->pretty_name));
}


status_t
MsvdxDecoder::Setup(media_format* ioEncodedFormat, const void* infoBuffer,
	size_t infoSize)
{
	fEncodedFormat = *ioEncodedFormat;
	fInfo.assign((const uint8*)infoBuffer,
		(const uint8*)infoBuffer + infoSize);
	fWidth = ioEncodedFormat->u.encoded_video.output.display.line_width;
	fHeight = ioEncodedFormat->u.encoded_video.output.display.line_count;

	const char* env = getenv("MSVDX_MEDIA");
	if ((env != NULL && strcmp(env, "0") == 0)
		|| fWidth > 1920 || fHeight > 1088 || rtv_msvdx_firmware() == NULL)
		return UseFfmpeg(NULL, 0, NULL);

	fHardware = rtv_msvdx_create(OnPicture, this);
	if (fHardware == NULL) {
		TRACE("hardware unavailable\n");
		return UseFfmpeg(NULL, 0, NULL);
	}
	TRACE("Setup %dx%d, %zu bytes of codec data\n", fWidth, fHeight, infoSize);

	// MP4 and Matroska carry SPS and PPS in an avcC box and length-prefixed
	// NAL units; MPEG-TS carries Annex B with no extradata.
	if (infoSize > 6 && ((const uint8*)infoBuffer)[0] == 1) {
		if (!ParseAvcC((const uint8*)infoBuffer, infoSize)
			|| rtv_msvdx_decode(fHardware, fAnnexB.data(), fAnnexB.size(), -1)
				!= 0) {
			rtv_msvdx_destroy(fHardware);
			fHardware = NULL;
			return UseFfmpeg(NULL, 0, NULL);
		}
	}
	return B_OK;
}


bool
MsvdxDecoder::ParseAvcC(const uint8* p, size_t size)
{
	static const uint8 kStart[4] = { 0, 0, 0, 1 };
	fNalLengthSize = (p[4] & 3) + 1;
	fAnnexB.clear();
	size_t at = 5;
	for (int set = 0; set < 2; set++) {
		if (at >= size)
			return false;
		int count = set == 0 ? (p[at] & 0x1f) : p[at];
		at++;
		for (int i = 0; i < count; i++) {
			if (at + 2 > size)
				return false;
			size_t length = (p[at] << 8) | p[at + 1];
			at += 2;
			if (at + length > size)
				return false;
			fAnnexB.insert(fAnnexB.end(), kStart, kStart + 4);
			fAnnexB.insert(fAnnexB.end(), p + at, p + at + length);
			at += length;
		}
	}
	return true;
}


void
MsvdxDecoder::ToAnnexB(const uint8* data, size_t size)
{
	static const uint8 kStart[4] = { 0, 0, 0, 1 };
	fAnnexB.clear();
	size_t at = 0;
	while (at + fNalLengthSize <= size) {
		size_t length = 0;
		for (int i = 0; i < fNalLengthSize; i++)
			length = (length << 8) | data[at + i];
		at += fNalLengthSize;
		if (length > size - at)
			break;
		fAnnexB.insert(fAnnexB.end(), kStart, kStart + 4);
		fAnnexB.insert(fAnnexB.end(), data + at, data + at + length);
		at += length;
	}
}


status_t
MsvdxDecoder::UseFfmpeg(const void* replay, size_t replaySize,
	const media_header* replayHeader)
{
	if (fFfmpeg != NULL)
		return B_OK;
	TRACE("handing the track to ffmpeg (replaying %zu bytes)\n", replaySize);

	// The system's own ffmpeg plugin, for this architecture.
	char** paths = NULL;
	size_t count = 0;
	if (find_paths_etc(get_architecture(), B_FIND_PATH_ADD_ONS_DIRECTORY,
			"media/plugins/ffmpeg", B_FIND_PATH_EXISTING_ONLY, &paths, &count)
			!= B_OK)
		return B_ERROR;
	for (size_t i = 0; i < count && fFfmpegImage < 0; i++) {
		if (strncmp(paths[i], "/boot/system/", 13) == 0)
			fFfmpegImage = load_add_on(paths[i]);
	}
	free(paths);
	if (fFfmpegImage < 0)
		return B_ERROR;

	MediaPlugin* (*instantiate)() = NULL;
	if (get_image_symbol(fFfmpegImage, "instantiate_plugin", B_SYMBOL_TYPE_TEXT,
			(void**)&instantiate) != B_OK)
		return B_ERROR;
	fFfmpegPlugin = instantiate();
	DecoderPlugin* plugin = dynamic_cast<DecoderPlugin*>(fFfmpegPlugin);
	if (plugin == NULL)
		return B_ERROR;
	fFfmpeg = plugin->NewDecoder(0);
	if (fFfmpeg == NULL)
		return B_ERROR;
	ChunkProvider* chunks = new(std::nothrow) FallbackChunks(this);
	if (chunks == NULL)
		return B_NO_MEMORY;
	fFfmpeg->SetChunkProvider(chunks);

	if (replay != NULL) {
		fReplay.assign((const uint8*)replay, (const uint8*)replay + replaySize);
		fReplayHeader = *replayHeader;
		fReplayPending = true;
	}

	media_format format = fEncodedFormat;
	status_t status = fFfmpeg->Setup(&format, fInfo.data(), fInfo.size());
	if (status == B_OK && fOutputNegotiated) {
		media_format output = fOutputFormat;
		status = fFfmpeg->NegotiateOutputFormat(&output);
	}
	return status;
}


status_t
MsvdxDecoder::GetNextChunk(const void** chunk, size_t* size,
	media_header* header)
{
	if (fReplayPending) {
		fReplayPending = false;
		*chunk = fReplay.data();
		*size = fReplay.size();
		*header = fReplayHeader;
		return B_OK;
	}
	return Decoder::GetNextChunk(chunk, size, header);
}


status_t
MsvdxDecoder::NegotiateOutputFormat(media_format* ioDecodedFormat)
{
	if (fFfmpeg != NULL) {
		status_t status = fFfmpeg->NegotiateOutputFormat(ioDecodedFormat);
		fOutputFormat = *ioDecodedFormat;
		fOutputNegotiated = true;
		return status;
	}

	color_space space = ioDecodedFormat->u.raw_video.display.format;
	if (space != B_RGB32 && space != B_RGBA32 && space != B_YCbCr422
		&& space != B_YCbCr420)
		space = B_RGB32;

	media_raw_video_format& raw = ioDecodedFormat->u.raw_video;
	const media_encoded_video_format& in = fEncodedFormat.u.encoded_video;
	ioDecodedFormat->type = B_MEDIA_RAW_VIDEO;
	raw = media_raw_video_format::wildcard;
	raw.field_rate = in.output.field_rate;
	raw.interlace = 1;
	raw.first_active = 0;
	raw.last_active = fHeight - 1;
	raw.orientation = B_VIDEO_TOP_LEFT_RIGHT;
	raw.pixel_width_aspect = in.output.pixel_width_aspect;
	raw.pixel_height_aspect = in.output.pixel_height_aspect;
	raw.display.format = space;
	raw.display.line_width = fWidth;
	raw.display.line_count = fHeight;
	raw.display.bytes_per_row = space == B_YCbCr420 ? fWidth
		: space == B_YCbCr422 ? fWidth * 2 : fWidth * 4;
	raw.display.pixel_offset = 0;
	raw.display.line_offset = 0;
	raw.display.flags = 0;

	fOutputFormat = *ioDecodedFormat;
	fOutputNegotiated = true;
	return B_OK;
}


status_t
MsvdxDecoder::SeekedTo(int64 frame, bigtime_t time)
{
	if (fFfmpeg != NULL)
		return fFfmpeg->SeekedTo(frame, time);
	rtv_msvdx_reset(fHardware);
	fPictures.clear();
	fDrained = false;
	return B_OK;
}


void
MsvdxDecoder::OnPicture(void* opaque, const rtv_msvdx_picture* p)
{
	MsvdxDecoder* self = (MsvdxDecoder*)opaque;
	Picture picture;
	picture.width = p->visible_width;
	picture.height = p->visible_height;
	picture.stride = p->stride;
	picture.pts = p->pts;
	// Keep only the visible part; the hardware surface is reused.
	const uint8* luma = p->luma + p->visible_y * p->stride + p->visible_x;
	const uint8* chroma = p->chroma + (p->visible_y / 2) * p->stride
		+ (p->visible_x & ~1);
	picture.luma.assign(luma, luma + (size_t)p->stride * (picture.height - 1)
		+ picture.width);
	picture.chroma.assign(chroma, chroma
		+ (size_t)p->stride * (picture.height / 2 - 1) + picture.width);
	self->fPictures.push_back(std::move(picture));
}


status_t
MsvdxDecoder::Convert(const Picture& picture, void* buffer)
{
	const media_raw_video_format& raw = fOutputFormat.u.raw_video;
	color_space space = raw.display.format;
	AVPixelFormat target = space == B_YCbCr420 ? AV_PIX_FMT_YUV420P
		: space == B_YCbCr422 ? AV_PIX_FMT_YUYV422 : AV_PIX_FMT_BGRA;
	int width = fWidth > 0 ? fWidth : picture.width;
	int height = fHeight > 0 ? fHeight : picture.height;

	if (fScaler == NULL || fScalerWidth != picture.width
		|| fScalerHeight != picture.height || fScalerSpace != space) {
		sws_freeContext(fScaler);
		fScaler = sws_getContext(picture.width, picture.height,
			AV_PIX_FMT_NV12, width, height, target, SWS_FAST_BILINEAR, NULL,
			NULL, NULL);
		fScalerWidth = picture.width;
		fScalerHeight = picture.height;
		fScalerSpace = space;
		if (fScaler == NULL)
			return B_ERROR;
	}

	const uint8_t* src[4] = { picture.luma.data(), picture.chroma.data(),
		NULL, NULL };
	int srcStride[4] = { picture.stride, picture.stride, 0, 0 };
	uint8_t* dst[4] = { (uint8_t*)buffer, NULL, NULL, NULL };
	int dstStride[4] = { (int)raw.display.bytes_per_row, 0, 0, 0 };
	if (target == AV_PIX_FMT_YUV420P) {
		dst[1] = dst[0] + (size_t)width * height;
		dst[2] = dst[1] + (size_t)(width / 2) * (height / 2);
		dstStride[1] = dstStride[2] = width / 2;
	}
	sws_scale(fScaler, src, srcStride, 0, picture.height, dst, dstStride);
	return B_OK;
}


status_t
MsvdxDecoder::Decode(void* buffer, int64* frameCount, media_header* header,
	media_decode_info* info)
{
	if (fFfmpeg != NULL)
		return fFfmpeg->Decode(buffer, frameCount, header, info);

	while (fPictures.empty()) {
		if (fDrained)
			return B_LAST_BUFFER_ERROR;

		const void* chunk;
		size_t size;
		media_header chunkHeader;
		status_t status = Decoder::GetNextChunk(&chunk, &size, &chunkHeader);
		if (status != B_OK) {
			TRACE("GetNextChunk: %s, draining\n", strerror(status));
			// End of the track: hand over what the DPB still holds.
			rtv_msvdx_drain(fHardware);
			fDrained = true;
			continue;
		}

		const uint8* data = (const uint8*)chunk;
		size_t length = size;
		if (fNalLengthSize > 0) {
			ToAnnexB(data, size);
			data = fAnnexB.data();
			length = fAnnexB.size();
		}
		if (rtv_msvdx_decode(fHardware, data, length, chunkHeader.start_time)
				!= 0) {
			TRACE("hardware decode failed at %lld us (%zu bytes)\n",
				(long long)chunkHeader.start_time, size);
			// Not a stream this hardware path handles. Give the track to
			// ffmpeg, starting with the chunk that failed. Any pictures
			// already out are dropped; this happens on the first frame.
			rtv_msvdx_destroy(fHardware);
			fHardware = NULL;
			fPictures.clear();
			if (UseFfmpeg(chunk, size, &chunkHeader) != B_OK)
				return B_ERROR;
			return fFfmpeg->Decode(buffer, frameCount, header, info);
		}
	}

	Picture picture = std::move(fPictures.front());
	fPictures.pop_front();
	TRACE("picture %dx%d pts %lld\n", picture.width, picture.height,
		(long long)picture.pts);
	status_t status = Convert(picture, buffer);
	if (status != B_OK)
		return status;

	memset(header, 0, sizeof(*header));
	header->type = B_MEDIA_RAW_VIDEO;
	header->start_time = picture.pts;
	header->size_used = fOutputFormat.u.raw_video.display.bytes_per_row
		* fOutputFormat.u.raw_video.display.line_count;
	header->u.raw_video.display_line_width = fWidth;
	header->u.raw_video.display_line_count = fHeight;
	header->u.raw_video.bytes_per_row
		= fOutputFormat.u.raw_video.display.bytes_per_row;
	header->u.raw_video.field_sequence = 0;
	header->u.raw_video.field_number = 0;
	header->u.raw_video.first_active_line = 0;
	header->u.raw_video.line_count = fHeight;
	*frameCount = 1;
	return B_OK;
}


class MsvdxDecoderPlugin : public DecoderPlugin {
public:
	Decoder* NewDecoder(uint index) override
	{
		return new(std::nothrow) MsvdxDecoder();
	}

	status_t GetSupportedFormats(media_format** formats, size_t* count) override
	{
		// The description the ffmpeg reader gives H.264 tracks.
		media_format_description description;
		description.family = B_MISC_FORMAT_FAMILY;
		description.u.misc.file_format = 'ffmp';
		description.u.misc.codec = AV_CODEC_ID_H264;

		static media_format format;
		format.Clear();
		format.type = B_MEDIA_ENCODED_VIDEO;
		format.u.encoded_video = media_encoded_video_format::wildcard;
		BMediaFormats mediaFormats;
		status_t status = mediaFormats.MakeFormatFor(&description, 1, &format);
		if (status != B_OK && status != B_MEDIA_DUPLICATE_FORMAT)
			return status;
		*formats = &format;
		*count = 1;
		return B_OK;
	}
};

}	// namespace


MediaPlugin*
instantiate_plugin()
{
	return new(std::nothrow) MsvdxDecoderPlugin();
}
