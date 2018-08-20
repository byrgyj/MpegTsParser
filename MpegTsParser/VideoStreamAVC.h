
#ifndef ES_H264_H
#define ES_H264_H

#include "elementaryStream.h"

namespace QIYI
{
  class VideoStreamAVC : public ElementaryStream
  {
  private:
    typedef struct h264_private
    {
      struct SPS
      {
        int frame_duration;
        int cbpsize;
        int pic_order_cnt_type;
        int frame_mbs_only_flag;
        int log2_max_frame_num;
        int log2_max_pic_order_cnt_lsb;
        int delta_pic_order_always_zero_flag;
      } sps[256];

      struct PPS
      {
        int sps;
        int pic_order_present_flag;
      } pps[256];

      struct VCL_NAL
      {
        int frame_num; // slice
        int pic_parameter_set_id; // slice
        int field_pic_flag; // slice
        int bottom_field_flag; // slice
        int delta_pic_order_cnt_bottom; // slice
        int delta_pic_order_cnt_0; // slice
        int delta_pic_order_cnt_1; // slice
        int pic_order_cnt_lsb; // slice
        int idr_pic_id; // slice
        int nal_unit_type;
        int nal_ref_idc; // start code
        int pic_order_cnt_type; // sps
        int slice_type;
      } vcl_nal;

    } h264_private_t;

    typedef struct mpeg_rational_s {
      int num;
      int den;
    } mpeg_rational_t;

    enum
    {
      NAL_SLH     = 0x01, // Slice Header
      NAL_SEI     = 0x06, // Supplemental Enhancement Information
      NAL_SPS     = 0x07, // Sequence Parameter Set
      NAL_PPS     = 0x08, // Picture Parameter Set
      NAL_AUD     = 0x09, // Access Unit Delimiter
      NAL_END_SEQ = 0x0A  // End of Sequence
    };

    uint32_t        m_StartCode;
    bool            m_NeedIFrame;
    bool            m_NeedSPS;
    bool            m_NeedPPS;
    int             m_Width;
    int             m_Height;
    int             m_FPS;
    int             m_FpsScale;
    mpeg_rational_t m_PixelAspect;
    int             m_FrameDuration;
    h264_private    m_streamData;
    int             m_vbvDelay;       /* -1 if CBR */
    int             m_vbvSize;        /* Video buffer size (in bytes) */
    int64_t         m_DTS;
    int64_t         m_PTS;
    bool            m_Interlaced;

    int parseAVC(uint32_t startcode, int buf_ptr, bool &complete);
    bool parsePPS(uint8_t *buf, int len);
    bool parseSliceHeader(uint8_t *buf, int len, h264_private::VCL_NAL &vcl);
    bool parseSPS(uint8_t *buf, int len);
    bool IsFirstVclNal(h264_private::VCL_NAL &vcl);

  public:
    VideoStreamAVC(uint16_t pes_pid);
    virtual ~VideoStreamAVC();

    virtual int64_t parse(const TsPacket*pkt);
    //virtual void Parse(STREAM_PKT* pkt);
    virtual void Reset();
  };
}

#endif /* ES_H264_H */
