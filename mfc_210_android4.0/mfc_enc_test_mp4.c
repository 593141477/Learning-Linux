#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>

#include "SsbSipMfcApi.h"
#include "mfc_interface.h"

#define INPUT_NAME "cam.yuv"
#define OUTPUT_NAME "mfc.m4v"
#define INPUT_WIDTH 640
#define INPUT_HEIGHT 480

SSBSIP_MFC_ENC_MPEG4_PARAM mp4_param;
SSBSIP_MFC_ENC_INPUT_INFO input_info;
SSBSIP_MFC_ENC_OUTPUT_INFO output_info;

int fd_yuv, fd_mp4;
void* mfc_handle = NULL;

void param_init(int width, int height)
{
    mp4_param.codecType    = MPEG4_ENC;
    mp4_param.SourceWidth  = width;
    mp4_param.SourceHeight = height;
    mp4_param.IDRPeriod    = 100;
    mp4_param.SliceMode    = 0;
    mp4_param.RandomIntraMBRefresh = 0;
    mp4_param.EnableFRMRateControl = 1;
    // mp4_param.Bitrate      = 3000;
    mp4_param.Bitrate      = 128000;
    mp4_param.FrameQp      = 20;
    mp4_param.FrameQp_P    = 20;

    mp4_param.QSCodeMax    = 30;
    mp4_param.QSCodeMin    = 10;

    mp4_param.CBRPeriodRf  = 120;

    mp4_param.PadControlOn = 0;             // 0: disable, 1: enable
    mp4_param.LumaPadVal   = 0;
    mp4_param.CbPadVal     = 0;
    mp4_param.CrPadVal     = 0;

    mp4_param.ProfileIDC = 0; // 0=main,1=high,2=baseline
    mp4_param.LevelIDC = 40; // level 4.0
    mp4_param.FrameQp_B = 20;
    mp4_param.TimeIncreamentRes = 0;
    mp4_param.VopTimeIncreament = 0;
    mp4_param.SliceArgument = 0;
    mp4_param.NumberBFrames = 0; //<=2
    mp4_param.DisableQpelME = 0;

    mp4_param.FrameMap = NV12_LINEAR;
}

void start_encoding(int width, int height)
{
    int ret_code;

    int frameYSize = (width) * (height);
    int frameCSize = frameYSize/2;
    // int frameYSize = ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height));
    // int frameCSize = ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height / 2));

    char *CbCrBuf, *pCb, *pCr, *pNV12;

    ret_code = SsbSipMfcEncGetOutBuf(mfc_handle, &output_info);
    if (ret_code != MFC_RET_OK) {
        puts("SsbSipMfcEncGetOutBuf Failed!!");
        return;
    }
    printf("OutputVirAddr: 0x%08x, HeaderSize: %d\n", output_info.StrmVirAddr, output_info.headerSize);

    if(write(fd_mp4, output_info.StrmVirAddr, output_info.headerSize) <= 0){
        perror("write header");
        return;
    }

    ret_code = SsbSipMfcEncGetInBuf(mfc_handle, &input_info);
    if (ret_code != MFC_RET_OK) {
        puts("SsbSipMfcEncGetInBuf Failed!!");
        exit(1);
    }

    printf("YVirAddr: 0x%08x, CVirAddr: 0x%08x\n", input_info.YVirAddr, input_info.CVirAddr);
    
    CbCrBuf = (char *)malloc(frameCSize);
    if(!CbCrBuf) {
        perror("malloc");
        exit(1);
    }
    for(;;) {
        int j;

        if(read(fd_yuv, input_info.YVirAddr, frameYSize) <= 0){
            perror("read Yframe");
            break;
        }
        if(read(fd_yuv, CbCrBuf/*input_info.CVirAddr*/, frameCSize) <= 0){
            perror("read Cframe");
            break;
        }

        // YV12 -> NV12
        pNV12 = input_info.CVirAddr;
        pCb = CbCrBuf;
        pCr = CbCrBuf + (frameCSize >> 1);
        for(j=0; j<(frameCSize>>1); j++){
            *pNV12 = *pCb;
            pNV12++;
            *pNV12 = *pCr;
            pNV12++;

            pCr++;
            pCb++;
        }

        ret_code = SsbSipMfcEncSetInBuf(mfc_handle, &input_info);
        if (ret_code != MFC_RET_OK) {
            puts("SsbSipMfcEncSetInBuf Failed!!");
            break;
        }
        //SsbSipMfcEncSetConfig(pHmp4Enc->hMFCHmp4Handle.hMFCHandle, MFC_ENC_SETCONF_FRAME_TAG, &(pHmp4Enc->hMFCHmp4Handle.indexTimestamp));

        ret_code = SsbSipMfcEncExe(mfc_handle);
        if(ret_code != MFC_RET_OK) {
            puts("SsbSipMfcEncExe Failed!!");
            break;
        }

        ret_code = SsbSipMfcEncGetOutBuf(mfc_handle, &output_info);
        if (ret_code != MFC_RET_OK) {
            puts("SsbSipMfcEncGetOutBuf Failed!!");
            break;
        }

        printf("OutputVirAddr: 0x%08x, FrameSize: %d\n", output_info.StrmVirAddr, output_info.dataSize);
        
        write(fd_mp4, output_info.StrmVirAddr, output_info.dataSize);

    }

}

int open_video_files()
{
    if((fd_yuv=open(INPUT_NAME, O_RDONLY)) < 0){
        perror("open source file");
        return 0;
    }
    if((fd_mp4=open(OUTPUT_NAME, O_RDWR|O_CREAT|O_TRUNC)) < 0){
        perror("open output file");
        return 0;
    }
    return 1;
}
int close_video_files()
{
    close(fd_mp4);
    close(fd_yuv);
}
int main()
{
    int ret_code;

    SSBIP_MFC_BUFFER_TYPE buf_type = CACHE;

    if(!open_video_files()){
        puts("open_video_files Failed!!");
        exit(1);
    }

    mfc_handle = SsbSipMfcEncOpen(&buf_type);
    if(!mfc_handle){
        puts("SsbSipMfcEncOpen Failed!!");
        exit(1);
    }

    // ret_code = SsbSipMfcEncSetSize(mfc_handle, mp4_ENC, width, height);
    // if (ret_code != MFC_RET_OK) {
    //     puts("SsbSipMfcEncSetSize Failed!!");
    //     exit(1);
    // }
    
    // ret_code = SsbSipMfcEncGetInBuf(mfc_handle, &input_info);
    // if (ret_code != MFC_RET_OK) {
    //     puts("SsbSipMfcEncGetInBuf Failed!!");
    //     exit(1);
    // }

    param_init(INPUT_WIDTH, INPUT_HEIGHT);

    ret_code = SsbSipMfcEncInit(mfc_handle, &mp4_param);
    if (ret_code != MFC_RET_OK) {
        puts("SsbSipMfcEncInit Failed!!");
        exit(1);
    }

    start_encoding(INPUT_WIDTH, INPUT_HEIGHT);

    SsbSipMfcEncClose(mfc_handle);

    close_video_files();
}