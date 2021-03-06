/************************************

libjpegライブラリを用いて、jpegファイルをグレーに変換するプログラムである。
グレーグラフを入力すると、そのまま出力する。
jpeg_std_errorでinputファイルをチェックできる。jpg,jpegではない場合、エラー

crash point:
JPEG fileのヘッダファイルにより、width、height、colorChannelsのデータが得られる。
このファイル性質によりalloc_jpegを使ってdecompress dataを保存する領域を作成する（jpegData->data）。
もしBuzzFuzzを使って、inputファイルのwidth、height、colorChannelsの位置のバイト値を小さい値に書き換えると、
decompress dataを読み込むときにoverflowが発生する。

*************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <jpeglib.h>

// JPEG image struct
typedef struct {
    uint8_t *data;   // raw data
    uint32_t width;
    uint32_t height;
    uint32_t ch;     // color channels
} JpegData;

// allocate memory for raw data
void alloc_jpeg(JpegData *jpegData)
{
    jpegData->data = NULL;
    jpegData->data = (uint8_t*) malloc(sizeof(uint8_t)  *
                                       jpegData->width  *
                                       jpegData->height *
                                       jpegData->ch);
}

// free memory for raw data
void free_jpeg(JpegData *jpegData)
{
    if (jpegData->data != NULL) {
        free(jpegData->data);
        jpegData->data = NULL;
    }
}

// read JPEG image
// 1. create JPEG decompression object
// 2. specify source data
// 3. read JPEG header
// 4. start decompression
// 5. scan lines
// 6. finish decompression
bool read_jpeg(JpegData *jpegData,           // JPEG image struct
              const char *srcfile,           // input JPEG file
              struct jpeg_error_mgr *jerr)   // error info
{
    // 1.
    struct jpeg_decompress_struct cinfo;     //データを扱うためのオブジェクト
    jpeg_create_decompress(&cinfo);          //初期化　
    cinfo.err = jpeg_std_error(jerr);

    FILE *fp = fopen(srcfile, "rb");         //open JPEG
    if (fp == NULL) {
        printf("Error: failed to open %s\n", srcfile);
        return false;
    }
    // 2.
    jpeg_stdio_src(&cinfo, fp);              //読み込むJPEG fileを定義

    // 3.
    jpeg_read_header(&cinfo, TRUE);          //ヘッダファイルを読み込む

    // 4.
    jpeg_start_decompress(&cinfo);           //start decompression


    if(cinfo.num_components==3){                  //RGB:3 , gray:1
         jpegData->width  = cinfo.image_width;    //input fileの性質をjpegDataに保存
         jpegData->height = cinfo.image_height;
         jpegData->ch     = cinfo.num_components; 
    }else if(cinfo.num_components==1){
         printf("is a gray image, needn't to convert\n");
         jpegData->width  = cinfo.image_width;    
         jpegData->height = cinfo.image_height;
         jpegData->ch     = cinfo.num_components; 
    }else{
         printf("wrong file\n");
         return false;
    }
    printf("width : %d\nheight : %d\n",jpegData->width,jpegData->height);
    /////////////////////////////////
    alloc_jpeg(jpegData);                    //allocate memory
    /////////////////////////////////

    // 5. read line by line
    uint8_t *row = jpegData->data;           //input JPEG fileのデータを読み込むメモリアドレスをrowに代入
    const uint32_t stride = jpegData->width * jpegData->ch;         //一行目のサイズを定義
    for (int y = 0; y < jpegData->height; y++) {                    //一行ずつ読み込む
    ///////////////CRASH POINT: OVERFLOW//////////////////   
        jpeg_read_scanlines(&cinfo, &row, 1);                       //cinfoからinput JPEG fileのデータを読み込み、rowに書き込む(jpegData->data)
        row += stride;                                              //アドレスは次の行の位置に移動
    }

    // 6.
    jpeg_finish_decompress(&cinfo);                                 //fininsh
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);

    return true;
}

// write JPEG image
// 1. create JPEG compression object
// 2. specify destination data
// 3. set parameters
// 4. start compression
// 5. scan lines
// 6. finish compression
bool write_jpeg(const JpegData *grayimage,
                const char *dstfile,                         //output file
                struct jpeg_error_mgr *jerr)
{
    // 1.
    struct jpeg_compress_struct cinfo;                       //compress struct
    jpeg_create_compress(&cinfo);                            //初期化
    cinfo.err = jpeg_std_error(jerr);

    FILE *fp = fopen(dstfile, "wb");                         //open output file
    if (fp == NULL) {
        printf("Error: failed to open %s\n", dstfile);
        return false;
    }
    // 2.
    jpeg_stdio_dest(&cinfo, fp);                             //書き込むファイルを定義

    // 3.
    cinfo.image_width      = grayimage->width;                //output JPEG fileの性質を定義
    cinfo.image_height     = grayimage->height;
    cinfo.input_components = grayimage->ch;                                    
    cinfo.in_color_space   = JCS_GRAYSCALE;                        //RGB: JCS_RGB, gray: JCS_GRAYSCALE
    jpeg_set_defaults(&cinfo);                               //set default

    // 4.
    jpeg_start_compress(&cinfo, TRUE);                       //start compress
    

    // 5.
    uint8_t *row = grayimage->data;                           //same as decompress
    const uint32_t stride = grayimage->width * grayimage->ch;  
    for (int y = 0; y < grayimage->height; y++) {
        jpeg_write_scanlines(&cinfo, &row, 1);               //rowからcinfoに書き込む
        row += stride;
    }

    // 6.
    jpeg_finish_compress(&cinfo);                            //finish
    jpeg_destroy_compress(&cinfo);
    fclose(fp);

    return true;
}

bool change_to_gray(const JpegData *jpegData,JpegData *grayimage)
{   
    
    grayimage->width      = jpegData->width;                
    grayimage->height     = jpegData->height;
    grayimage->ch         = 1;                           
    alloc_jpeg(grayimage);  
    if(jpegData->ch==1){    //gray imageなので、そのまま出力
        for (int y = 0; y < jpegData->height*jpegData->width*jpegData->ch; y++) {
             grayimage->data[y]=jpegData->data[y];
        }
    }else{                  //RGB convert to gray
        for (int y = 0; y < jpegData->height*jpegData->width*jpegData->ch; y++) {
             if(y%3==0){
                 grayimage->data[y/3]=(jpegData->data[y]*299+
                                       jpegData->data[y+1]*587+
                                       jpegData->data[y+2]*114+
                                       500)/1000;
             }
        }
    }

    return true;
}

int main(int argc, char* argv[])
{    
    if (argc != 2)
    {
        printf("Usage: \n");
        printf("%s <jpg_file>\n", argv[0]);
        return -1;
    }
    //gray image struct
    JpegData grayimage;
    JpegData jpegData;
    struct jpeg_error_mgr jerr;

    // src/dst file
    char *src = argv[1];
    char *dst = "/home/binary/BuzzFuzz/program/jpeg/output_gray.jpg";

    //decompress input JPEG file
    if (!read_jpeg(&jpegData, src, &jerr)){           
        free_jpeg(&jpegData);
        return -1;
    }
    printf("Read:  %s\n", src);
    
    //convert image to gray image
    if (!change_to_gray(&jpegData,&grayimage)){                      
        free_jpeg(&jpegData);
        free_jpeg(&grayimage);
        return -1;
    }
    

    //compress
    if (!write_jpeg(&grayimage, dst, &jerr)){                      
        free_jpeg(&grayimage);
        return -1;
    }
    printf("Write: %s\n", dst);

    free_jpeg(&grayimage);
    free_jpeg(&jpegData);
    return 0;
}
