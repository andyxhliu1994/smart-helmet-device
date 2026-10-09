#include "rknn.h"

rknn_context ctx;
rknn_input_output_num io_num;
rknn_tensor_attr output_attrs[3];
unsigned char *model_data = NULL;
const float nms_threshold = NMS_THRESH;      // 默认的NMS阈值
const float box_conf_threshold = BOX_THRESH; // 默认的置信度阈值
int model_channel = 3;
int model_width = 0;
int model_height = 0;

static void dump_tensor_attr(rknn_tensor_attr *attr)
{
  char shape_str[512] = {0}; 
  char tmp[16] = {0};
  if( attr->n_dims >= 1 ){
    sprintf( tmp, "%d ", attr->dims[0]);
    strcpy( shape_str, tmp );
  }

  for (uint32_t i = 1; i < attr->n_dims; ++i) {
    sprintf( tmp, ", %d", attr->dims[i] );
    strcat( shape_str, tmp );
  }

  printf("  index=%d, name=%s, n_dims=%d, dims=[%s], n_elems=%d, size=%d, w_stride = %d, size_with_stride=%d, fmt=%s, "
         "type=%s, qnt_type=%s, "
         "zp=%d, scale=%f\n",
         attr->index, attr->name, attr->n_dims, shape_str, attr->n_elems, attr->size, attr->w_stride,
         attr->size_with_stride, get_format_string(attr->fmt), get_type_string(attr->type),
         get_qnt_type_string(attr->qnt_type), attr->zp, attr->scale);
}

static unsigned char *load_data(FILE *fp, size_t ofst, size_t sz)
{
  unsigned char *data = NULL;
  int ret;

  if (NULL == fp)
    return NULL;
  
  ret = fseek(fp, ofst, SEEK_SET);
  if (ret != 0) {
    RK_LOGE("blob seek failure.\n");
    return NULL;
  }

  data = (unsigned char *)malloc(sz);
  if (data == NULL){
    RK_LOGE("buffer malloc failure.\n");
    return NULL;
  }
  ret = fread(data, 1, sz, fp);
  return data;
}

static unsigned char *load_model(const char *filename, int *model_size)
{
  FILE *fp;
  unsigned char *data;
  fp = fopen(filename, "rb");
  if (NULL == fp) {
    RK_LOGE("Open file %s failed.\n", filename);
    return NULL;
  }
  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  data = load_data(fp, 0, size);
  fclose(fp);
  *model_size = size;
  return data;
}

int init_rknn( void )
{
    if( strlen( pRKnnModelPath ) <= 3 ){
        RK_LOGI("rknn not work, return\n");
        return -1;  
    }
    printf("==============Start init rknn===============\n");

    int ret;
    printf("Loading model %s...\n", pRKnnModelPath );
    int model_data_size = 0;
    model_data = load_model( pRKnnModelPath, &model_data_size);
    ret = rknn_init(&ctx, model_data, model_data_size, 0, NULL);
    if (ret < 0 ){
        RK_LOGE("rknn_init error ret=%d\n", ret);
        return -1;
    }

    rknn_sdk_version version;
    ret = rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(rknn_sdk_version));
    if (ret < 0) {
        RK_LOGE("rknn_init error ret=%d\n", ret);
        return -1;
    }
    printf("sdk version: %s driver version: %s\n", version.api_version, version.drv_version);

    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0) {
        RK_LOGE("rknn_init error ret=%d\n", ret);
        return -1;
    }
    printf("model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);

    rknn_tensor_attr input_attrs[io_num.n_input];
    memset(input_attrs, 0, sizeof(input_attrs));
    for (uint32_t i = 0; i < io_num.n_input; i++){
        input_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0){
            RK_LOGE("rknn_init error ret=%d\n", ret);
            return -1;
        }
        dump_tensor_attr(&(input_attrs[i]));
    }

    memset(output_attrs, 0, sizeof(output_attrs));
    for (uint32_t i = 0; i < io_num.n_output; i++) {
        output_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
        dump_tensor_attr(&(output_attrs[i]));
    }

    if (input_attrs[0].fmt == RKNN_TENSOR_NCHW){
        printf("model is NCHW input fmt\n");
        model_channel = input_attrs[0].dims[1];
        model_height  = input_attrs[0].dims[2];
        model_width   = input_attrs[0].dims[3];
    }else{
        printf("model is NHWC input fmt\n");
        model_height = input_attrs[0].dims[1];
        model_width  = input_attrs[0].dims[2];
        model_channel = input_attrs[0].dims[3];
    }
    printf("model input height=%d, width=%d, channel=%d\n", model_height, model_width, model_channel);

    return 0;
}

#if 0
void nv12_to_rgb(uint8_t* nv12, uint8_t* rgb, int width, int height) {
    int frameSize = width * height;
    uint8_t* yPlane = nv12;
    uint8_t* uvPlane = nv12 + frameSize;

    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            int yIndex = j * width + i;
            int uvIndex = (j / 2) * width + (i & ~1);
            uint8_t Y = yPlane[yIndex];
            uint8_t U = uvPlane[uvIndex];
            uint8_t V = uvPlane[uvIndex + 1];

            int C = Y - 16;
            int D = U - 128;
            int E = V - 128;

            int R = (298 * C + 409 * E + 128) >> 8;
            int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
            int B = (298 * C + 516 * D + 128) >> 8;

            R = R < 0 ? 0 : (R > 255 ? 255 : R);
            G = G < 0 ? 0 : (G > 255 ? 255 : G);
            B = B < 0 ? 0 : (B > 255 ? 255 : B);

            int rgbIndex = (j * width + i) * 3;
            rgb[rgbIndex + 0] = R;
            rgb[rgbIndex + 1] = G;
            rgb[rgbIndex + 2] = B;
        }
    }
}
#endif

int vi_rknn_data( uint32_t width, uint32_t height, void* data )
{
    struct timeval start_time, stop_time;
    int ret = 0;

    gettimeofday(&start_time, NULL);

    int src_width, src_height, src_format;
    int dst_width, dst_height, dst_format;
    char *src_buf, *dst_buf;
    int src_buf_size, dst_buf_size;
    rga_buffer_t src_img, dst_img;
    rga_buffer_handle_t src_handle, dst_handle;

    memset(&src_img, 0, sizeof(src_img));
    memset(&dst_img, 0, sizeof(dst_img));

    src_width  = width;
    src_height = height;
    src_format = RK_FORMAT_YCbCr_420_SP;
    dst_width  = model_width;
    dst_height = model_height;
    dst_format = RK_FORMAT_RGB_888;

    src_buf_size = src_width * src_height * get_bpp_from_format(src_format);
    dst_buf_size = dst_width * dst_height * get_bpp_from_format(dst_format);
    src_buf = (char *)malloc(src_buf_size);
    dst_buf = (char *)malloc(dst_buf_size);

    memcpy( src_buf, data,  src_buf_size );
    memset( dst_buf, 0x80,  dst_buf_size );

    src_handle = importbuffer_virtualaddr(src_buf, src_buf_size);
    dst_handle = importbuffer_virtualaddr(dst_buf, dst_buf_size);
    if (src_handle == 0 || dst_handle == 0) {
        printf("importbuffer failed!\n");
        if (src_handle) releasebuffer_handle(src_handle);
        if (dst_handle) releasebuffer_handle(dst_handle);
        if (src_buf)    free(src_buf);
        if (dst_buf)    free(dst_buf);
        return -3;   
    }
    src_img = wrapbuffer_handle(src_handle, src_width, src_height, src_format);
    dst_img = wrapbuffer_handle(dst_handle, dst_width, dst_height, dst_format);

    ret = imcheck(src_img, dst_img, {}, {});
    if (IM_STATUS_NOERROR != ret) {
        printf("%d, check error! %s", __LINE__, imStrError((IM_STATUS)ret));
        return -1;
    }

    ret = imcvtcolor(src_img, dst_img, src_format, dst_format);
    if (ret != IM_STATUS_SUCCESS) {
        printf("running failed, %s\n", imStrError((IM_STATUS)ret));
        if (src_handle) releasebuffer_handle(src_handle);
        if (dst_handle) releasebuffer_handle(dst_handle);
        if (src_buf)    free(src_buf);
        if (dst_buf)    free(dst_buf);
        return -3;
    }
        
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].size = model_width * model_height * model_channel ;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].pass_through = 0; 
    inputs[0].buf = dst_buf;
    rknn_inputs_set(ctx, io_num.n_input, inputs);

    rknn_output outputs[io_num.n_output];
    memset(outputs, 0, sizeof(outputs));
    for (uint32_t i = 0; i < io_num.n_output; i++) {
        outputs[i].index = i;
        outputs[i].want_float = 0;
    }

    // 执行推理
    ret = rknn_run(ctx, NULL);
    ret = rknn_outputs_get(ctx, io_num.n_output, outputs, NULL);

    detect_result_group_t detect_result_group;
    std::vector<float> out_scales;
    std::vector<int32_t> out_zps;
    for (int i = 0; i < io_num.n_output; ++i) {
        out_scales.push_back(output_attrs[i].scale);
        out_zps.push_back(output_attrs[i].zp);
    }
    BOX_RECT pads;
    memset(&pads, 0, sizeof(BOX_RECT));

    // 后处理
    post_process((int8_t *)outputs[0].buf, (int8_t *)outputs[1].buf, (int8_t *)outputs[2].buf, 640, 640,
               box_conf_threshold, nms_threshold, pads, 1, 1, out_zps, out_scales, &detect_result_group);
    gettimeofday(&stop_time, NULL);
    printf("\n\nRKNN size:%d once run use %.2f ms\n", 
            detect_result_group.count, (__get_us(stop_time) - __get_us(start_time)) / 1000);

    pthread_mutex_lock(&obj_det_update_lock);
    int x_min =1000000; int y_min = 100000; 	
	  int x_max =0;       int y_max = 0;

    // 画框和概率
    for (int i = 0; i < detect_result_group.count; i++) {
        detect_result_t *det_result = &(detect_result_group.results[i]);
        int x1 = det_result->box.left;
        int y1 = det_result->box.top;
        int x2 = det_result->box.right;
        int y2 = det_result->box.bottom;
        if( g_debug )
          printf("%s @ (%d %d %d %d) %.2f\n", det_result->name, x1, y1,x2, y2, det_result->prop);
            
        if( i < OBJ_DET_OSD_MAX ){
            obj_det_info.obj_det[i].enable = 1;
            obj_det_info.obj_det[i].color = ARGB_mediumblue ;
            obj_det_info.obj_det[i].score = det_result->prop ;
            obj_det_info.obj_det[i].start_x = x1 /640.0f;
            obj_det_info.obj_det[i].start_y = y1 /640.0f;
            int w = x2 - x1 ;
            int h = y2 - y1 ;
            obj_det_info.obj_det[i].width = w / 640.0f;
            obj_det_info.obj_det[i].hight = h / 640.0f;

            x_min = mmin( x_min,  x1 );
            y_min = mmin( y_min,  y1 );
            x_max = mmax( x_max,  x2 );
            y_max = mmax( y_max,  y2 );
        }     
#if 0        
        im_rect dst_rect = {};
        dst_rect.x = x1;
        dst_rect.y = y1;
        dst_rect.width  = x2 - x1 ;
        dst_rect.height = y2 - y1 ;

        if( ( dst_rect.width  <= 3 ) || ( dst_rect.height <= 3 ) )
          continue;

        ret = imcheck({}, dst_img, {}, dst_rect, IM_COLOR_FILL);
        if (IM_STATUS_NOERROR != ret) {
            printf("%d, check error! %s \n", __LINE__, imStrError((IM_STATUS)ret));
        }
        ret = imrectangle( dst_img, dst_rect, 0xff00ff00, 2);
        if (ret != IM_STATUS_SUCCESS) {
            printf("imrectangle running failed, %s\n", imStrError((IM_STATUS)ret));
        }
#endif            
    }

    if( detect_result_group.count ){
      obj_det_info.x_start = x_min /640.0f;
      obj_det_info.y_start = y_min /640.0f;
      obj_det_info.w_whole = ( x_max - x_min  ) /640.0f;
      obj_det_info.h_whole = ( y_max - y_min  ) /640.0f;	
    }
    obj_det_info.num = detect_result_group.count;
    pthread_mutex_unlock(&obj_det_update_lock);
    sem_post( obj_det_sem );
    
    if (save_file && ( FILE_RKNN == rawFarmeRecType ) ) {
        fwrite( dst_buf, 640*640*3, 1, save_file);
        fflush( save_file );
    }

    ret = rknn_outputs_release(ctx, io_num.n_output, outputs);
    if (src_handle) releasebuffer_handle(src_handle);
    if (dst_handle) releasebuffer_handle(dst_handle);
    if (src_buf)    free(src_buf);
    if (dst_buf)    free(dst_buf);

    return 0;
}

int deinit_rknn(void)
{
    if( strlen( pRKnnModelPath ) <= 3 ){
        RK_LOGI("rknn not work, do not need deinit rknn, return\n");
        return -1;  
    }
    printf("==============deinit rknn===============\n");

    deinitPostProcess();
    rknn_destroy(ctx);

    if (model_data)
        free(model_data);
    return 0;
}


/******************************************************************************/ 

static char *labels[OBJ_CLASS_NUM];

const int anchor0[6] = {10, 13, 16, 30, 33, 23};
const int anchor1[6] = {30, 61, 62, 45, 59, 119};
const int anchor2[6] = {116, 90, 156, 198, 373, 326};

inline static int clamp(float val, int min, int max) { return val > min ? (val < max ? val : max) : min; }

char *readLine(FILE *fp, char *buffer, int *len)
{
  int ch;
  int i = 0;
  size_t buff_len = 0;

  buffer = (char *)malloc(buff_len + 1);
  if (!buffer)
    return NULL; // Out of memory

  while ((ch = fgetc(fp)) != '\n' && ch != EOF) {
    buff_len++;
    void *tmp = realloc(buffer, buff_len + 1);
    if (tmp == NULL) {
      free(buffer);
      return NULL; // Out of memory
    }
    buffer = (char *)tmp;

    buffer[i] = (char)ch;
    i++;
  }
  buffer[i] = '\0';

  *len = buff_len;
  // Detect end
  if (ch == EOF && (i == 0 || ferror(fp))) {
    free(buffer);
    return NULL;
  }
  return buffer;
}

int readLines(const char *fileName, char *lines[], int max_line)
{
  FILE *file = fopen(fileName, "r");
  char *s;
  int i = 0;
  int n = 0;

  if (file == NULL){
    printf("Open %s fail!\n", fileName);
    return -1;
  }

  while ((s = readLine(file, s, &n)) != NULL) {
    lines[i++] = s;
    if (i >= max_line)
      break;
  }
  fclose(file);
  return i;
}

int loadLabelName(const char *locationFilename, char *label[])
{
  printf("loadLabelName %s\n", locationFilename);
  readLines(locationFilename, label, OBJ_CLASS_NUM);
  return 0;
}

static float CalculateOverlap(float xmin0, float ymin0, float xmax0, float ymax0, float xmin1, float ymin1, float xmax1,
                              float ymax1)
{
  float w = fmax(0.f, fmin(xmax0, xmax1) - fmax(xmin0, xmin1) + 1.0);
  float h = fmax(0.f, fmin(ymax0, ymax1) - fmax(ymin0, ymin1) + 1.0);
  float i = w * h;
  float u = (xmax0 - xmin0 + 1.0) * (ymax0 - ymin0 + 1.0) + (xmax1 - xmin1 + 1.0) * (ymax1 - ymin1 + 1.0) - i;
  return u <= 0.f ? 0.f : (i / u);
}

static int nms(int validCount, std::vector<float> &outputLocations, std::vector<int> classIds, std::vector<int> &order,
               int filterId, float threshold)
{
  for (int i = 0; i < validCount; ++i)
  {
    int n = order[i];
    if (n == -1 || classIds[n] != filterId)
      continue;
    
    for (int j = i + 1; j < validCount; ++j){
      int m = order[j];
      if (m == -1 || classIds[m] != filterId)
        continue;
      
      float xmin0 = outputLocations[n * 4 + 0];
      float ymin0 = outputLocations[n * 4 + 1];
      float xmax0 = outputLocations[n * 4 + 0] + outputLocations[n * 4 + 2];
      float ymax0 = outputLocations[n * 4 + 1] + outputLocations[n * 4 + 3];

      float xmin1 = outputLocations[m * 4 + 0];
      float ymin1 = outputLocations[m * 4 + 1];
      float xmax1 = outputLocations[m * 4 + 0] + outputLocations[m * 4 + 2];
      float ymax1 = outputLocations[m * 4 + 1] + outputLocations[m * 4 + 3];

      float iou = CalculateOverlap(xmin0, ymin0, xmax0, ymax0, xmin1, ymin1, xmax1, ymax1);

      if (iou > threshold)
        order[j] = -1;
    }
  }
  return 0;
}

static int quick_sort_indice_inverse(std::vector<float> &input, int left, int right, std::vector<int> &indices)
{
  float key;
  int key_index;
  int low = left;
  int high = right;
  if (left < right)
  {
    key_index = indices[left];
    key = input[left];
    while (low < high)
    {
      while (low < high && input[high] <= key)
        high--;
    
      input[low] = input[high];
      indices[low] = indices[high];
      while (low < high && input[low] >= key)
        low++;
      
      input[high] = input[low];
      indices[high] = indices[low];
    }
    input[low] = key;
    indices[low] = key_index;
    quick_sort_indice_inverse(input, left, low - 1, indices);
    quick_sort_indice_inverse(input, low + 1, right, indices);
  }
  return low;
}

static float sigmoid(float x) { return 1.0 / (1.0 + expf(-x)); }

static float unsigmoid(float y) { return -1.0 * logf((1.0 / y) - 1.0); }

inline static int32_t __clip(float val, float min, float max)
{
  float f = val <= min ? min : (val >= max ? max : val);
  return f;
}

static int8_t qnt_f32_to_affine(float f32, int32_t zp, float scale)
{
  float dst_val = (f32 / scale) + zp;
  int8_t res = (int8_t)__clip(dst_val, -128, 127);
  return res;
}

static float deqnt_affine_to_f32(int8_t qnt, int32_t zp, float scale) { return ((float)qnt - (float)zp) * scale; }

static int process(int8_t *input, int *anchor, int grid_h, int grid_w, int height, int width, int stride,
                   std::vector<float> &boxes, std::vector<float> &objProbs, std::vector<int> &classId, float threshold,
                   int32_t zp, float scale)
{
  int validCount = 0;
  int grid_len = grid_h * grid_w;
  int8_t thres_i8 = qnt_f32_to_affine(threshold, zp, scale);
  for (int a = 0; a < 3; a++)
  {
    for (int i = 0; i < grid_h; i++)
    {
      for (int j = 0; j < grid_w; j++)
      {
        int8_t box_confidence = input[(PROP_BOX_SIZE * a + 4) * grid_len + i * grid_w + j];
        if (box_confidence >= thres_i8)
        {
          int offset = (PROP_BOX_SIZE * a) * grid_len + i * grid_w + j;
          int8_t *in_ptr = input + offset;
          float box_x = (deqnt_affine_to_f32(*in_ptr, zp, scale)) * 2.0 - 0.5;
          float box_y = (deqnt_affine_to_f32(in_ptr[grid_len], zp, scale)) * 2.0 - 0.5;
          float box_w = (deqnt_affine_to_f32(in_ptr[2 * grid_len], zp, scale)) * 2.0;
          float box_h = (deqnt_affine_to_f32(in_ptr[3 * grid_len], zp, scale)) * 2.0;
          box_x = (box_x + j) * (float)stride;
          box_y = (box_y + i) * (float)stride;
          box_w = box_w * box_w * (float)anchor[a * 2];
          box_h = box_h * box_h * (float)anchor[a * 2 + 1];
          box_x -= (box_w / 2.0);
          box_y -= (box_h / 2.0);

          int8_t maxClassProbs = in_ptr[5 * grid_len];
          int maxClassId = 0;
          for (int k = 1; k < OBJ_CLASS_NUM; ++k) {
            int8_t prob = in_ptr[(5 + k) * grid_len];
            if (prob > maxClassProbs) {
              maxClassId = k;
              maxClassProbs = prob;
            }
          }
          if (maxClassProbs > thres_i8) {
            objProbs.push_back((deqnt_affine_to_f32(maxClassProbs, zp, scale)) * (deqnt_affine_to_f32(box_confidence, zp, scale)));
            classId.push_back(maxClassId);
            validCount++;
            boxes.push_back(box_x);
            boxes.push_back(box_y);
            boxes.push_back(box_w);
            boxes.push_back(box_h);
          }
        }
      }
    }
  }
  return validCount;
}

int post_process(int8_t *input0, int8_t *input1, int8_t *input2, int model_in_h, int model_in_w, float conf_threshold,
                 float nms_threshold, BOX_RECT pads, float scale_w, float scale_h, std::vector<int32_t> &qnt_zps,
                 std::vector<float> &qnt_scales, detect_result_group_t *group)
{
  static int init = -1;
  if (init == -1) {
    int ret = 0;
    ret = loadLabelName( pRKnnModelLabelPath, labels);
    if (ret < 0)
      return -1;
    init = 0;
  }
  memset(group, 0, sizeof(detect_result_group_t));

  std::vector<float> filterBoxes;
  std::vector<float> objProbs;
  std::vector<int> classId;

  // stride 8
  int stride0 = 8;
  int grid_h0 = model_in_h / stride0;
  int grid_w0 = model_in_w / stride0;
  int validCount0 = 0;
  validCount0 = process(input0, (int *)anchor0, grid_h0, grid_w0, model_in_h, model_in_w, stride0, filterBoxes, objProbs,
                        classId, conf_threshold, qnt_zps[0], qnt_scales[0]);

  // stride 16
  int stride1 = 16;
  int grid_h1 = model_in_h / stride1;
  int grid_w1 = model_in_w / stride1;
  int validCount1 = 0;
  validCount1 = process(input1, (int *)anchor1, grid_h1, grid_w1, model_in_h, model_in_w, stride1, filterBoxes, objProbs,
                        classId, conf_threshold, qnt_zps[1], qnt_scales[1]);

  // stride 32
  int stride2 = 32;
  int grid_h2 = model_in_h / stride2;
  int grid_w2 = model_in_w / stride2;
  int validCount2 = 0;
  validCount2 = process(input2, (int *)anchor2, grid_h2, grid_w2, model_in_h, model_in_w, stride2, filterBoxes, objProbs,
                        classId, conf_threshold, qnt_zps[2], qnt_scales[2]);

  int validCount = validCount0 + validCount1 + validCount2;
  // no object detect
  if (validCount <= 0)
    return 0;
  
  std::vector<int> indexArray;
  for (int i = 0; i < validCount; ++i)
    indexArray.push_back(i);
  
  quick_sort_indice_inverse(objProbs, 0, validCount - 1, indexArray);
  std::set<int> class_set(std::begin(classId), std::end(classId));
  for (auto c : class_set)
    nms(validCount, filterBoxes, classId, indexArray, c, nms_threshold);
  
  int last_count = 0;
  group->count = 0;
  /* box valid detect target */
  for (int i = 0; i < validCount; ++i)
  {
    if (indexArray[i] == -1 || last_count >= OBJ_NUMB_MAX_SIZE)
      continue;
    
    int n = indexArray[i];

    float x1 = filterBoxes[n * 4 + 0] - pads.left;
    float y1 = filterBoxes[n * 4 + 1] - pads.top;
    float x2 = x1 + filterBoxes[n * 4 + 2];
    float y2 = y1 + filterBoxes[n * 4 + 3];
    int id = classId[n];
    float obj_conf = objProbs[i];

    group->results[last_count].box.left = (int)(clamp(x1, 0, model_in_w) / scale_w);
    group->results[last_count].box.top = (int)(clamp(y1, 0, model_in_h) / scale_h);
    group->results[last_count].box.right = (int)(clamp(x2, 0, model_in_w) / scale_w);
    group->results[last_count].box.bottom = (int)(clamp(y2, 0, model_in_h) / scale_h);
    group->results[last_count].prop = obj_conf;
    char *label = labels[id];
    strncpy(group->results[last_count].name, label, OBJ_NAME_MAX_SIZE);

    // printf("result %2d: (%4d, %4d, %4d, %4d), %s\n", i, group->results[last_count].box.left,
    // group->results[last_count].box.top,
    //        group->results[last_count].box.right, group->results[last_count].box.bottom, label);
    last_count++;
  }
  group->count = last_count;

  return 0;
}

void deinitPostProcess()
{
  for (int i = 0; i < OBJ_CLASS_NUM; i++) {
    if (labels[i] != nullptr) {
      free(labels[i]);
      labels[i] = nullptr;
    }
  }
}


