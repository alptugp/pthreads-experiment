#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "Utils.h"
#include "Picture.h"
#include "PicProcess.h"
#include "Thpool.h"
#include <sys/time.h>

#define BLUR_REGION_SIZE 9
#define TEST_ITERATIONS 6
#define max(a, b) ((a) > (b) ? (a) : (b))

struct col_blurring_task_args {
  struct picture *pic;
  struct picture *tmp;
  int col;
};

struct row_blurring_task_args {
  struct picture *pic;
  struct picture *tmp;
  int row;
};

struct sector_blurring_task_args {
  struct picture *pic;
  struct picture *tmp;
  int width_start;
  int heigth_start;
  int width_sector_size;
  int heigth_sector_size;
};

static void pixel_blur(struct picture *pic, struct picture *tmp, int a, int b);
static long long get_curr_time();
static void col_blurring_task(void *args_ptr);
static void parallel_col_blur(struct picture *pic);
static void row_blurring_task(void *args_ptr);
static void parallel_row_blur(struct picture *pic);
static void sector_blurring_task(void *args_ptr);
static void parallel_sector_blur(struct picture *pic);
static void blur_picture_wrapped(struct picture *pic);
static void parallel_blur_picture_wrapped (struct picture *pic);


  // function pointer look-up table for picture transformation functions
  static void (* const cmds[])(struct picture *) = { 
    parallel_row_blur,
    parallel_col_blur,
    parallel_blur_picture_wrapped,
    blur_picture_wrapped,
    parallel_sector_blur
  };

  // list of all possible picture transformations
  static char *cmd_strings[] = { 
    "parallel_row_blur",
    "parallel_col_blur",
    "parallel_pixel_blur",
    "blur_picture",
    "parallel_sector_blur"
  };

  // size of look-up table (for safe IO error reporting)
  static int no_of_cmds = sizeof(cmds) / sizeof(cmds[0]);

// ---------- MAIN PROGRAM ---------- \\

  int main(int argc, char **argv) {

    printf("Support Code for Running the Blur Optimisation Experiments... \n");

    // capture and check command line arguments
    const char * filename = argv[1];
    const char * target_file = argv[2];
    const char * process = argv[3];
    
    if(filename == NULL || target_file == NULL || process == NULL){
      printf("[!] insufficient command line arguments provided\n");
      exit(IO_ERROR);
    }        

    printf("  filename  = %s\n", filename);
    printf("  target    = %s\n", target_file);
    printf("  process   = %s\n", process);


    // create original image object
    struct picture pic;
    if(!init_picture_from_file(&pic, filename)){
      exit(IO_ERROR);   
    }   

    // identify the picture transformation to run
    int cmd_no = 0;
    while(cmd_no < no_of_cmds && strcmp(process, cmd_strings[cmd_no])){
      cmd_no++;
    }

    // IO error check
    if(cmd_no == no_of_cmds) {
      printf("[!] invalid process requested: %s is not defined\n    aborting...\n", process);  
      exit(IO_ERROR);   
    }
  
    // dispatch to appropriate picture transformation function
    int time_sum = 0;
    for (int iteration = 0; iteration < TEST_ITERATIONS; iteration++) {
      printf("Iteration %d is running\n", iteration);
      long long start = get_curr_time();
      cmds[cmd_no](&pic);
      long long end = get_curr_time();
      time_sum += end - start;
    }

    int average = time_sum / TEST_ITERATIONS;

    // save resulting picture and report success
    save_picture_to_file(&pic, target_file);
    printf("-- Picture has been blurred with an average of %d milliseconds\n", average);
    
    clear_picture(&pic);
    return 0;
  }

static void parallel_col_blur(struct picture *pic) {
  // make temporary copy of picture to work from
  struct picture tmp;
  tmp.img = copy_image(pic->img);
  tmp.width = pic->width;
  tmp.height = pic->height;  
  
  threadpool thpool = thpool_init(pic->width - 2);

  // iterate over each column in the picture (ignoring boundary pixels)
  for(int b = 1 ; b < tmp.width - 1; b++) {
    struct col_blurring_task_args *params = (struct col_blurring_task_args*) malloc(sizeof(struct col_blurring_task_args)); 
    params->pic = pic;
    params->tmp = &tmp;
    params->col = b;

    thpool_add_work(thpool, (void *) col_blurring_task, (void *) params);
  }
        
  thpool_wait(thpool);
  thpool_destroy(thpool);  

  // temporary picture clean-up
  clear_picture(&tmp);
}

static void col_blurring_task(void *args_ptr) {
  struct col_blurring_task_args *args = (struct col_blurring_task_args *) args_ptr;
  struct picture *pic = (struct picture *) args->pic;
  struct picture *tmp = (struct picture *) args->tmp;
  int col = (int) args->col;

  int col_end = tmp->height - 1;
  for (int b = 1; b < col_end; b++) {
    pixel_blur(pic, tmp, col, b);
  }

  free(args);
}

static void parallel_row_blur(struct picture *pic) {
  // make temporary copy of picture to work from
  struct picture tmp;
  tmp.img = copy_image(pic->img);
  tmp.width = pic->width;
  tmp.height = pic->height;  

  threadpool thpool = thpool_init(pic->height - 2);

  // iterate over each row in the picture (ignoring boundary pixels)
  for(int a = 1 ; a < tmp.height - 1; a++) {
    struct row_blurring_task_args *params = (struct row_blurring_task_args*) malloc(sizeof(struct row_blurring_task_args)); 
    params->pic = pic;
    params->tmp = &tmp;
    params->row = a;
    
    thpool_add_work(thpool, (void *) row_blurring_task, (void *) params);
  }

  thpool_wait(thpool);
  thpool_destroy(thpool);  

  // temporary picture clean-up
  clear_picture(&tmp);  
}

static void row_blurring_task(void *args_ptr) {
  struct row_blurring_task_args *args = (struct row_blurring_task_args *) args_ptr;
  struct picture *pic = (struct picture *) args->pic;
  struct picture *tmp = (struct picture *) args->tmp;
  int row = (int) args->row;

  int row_end = tmp->width - 1;
  for (int a = 1; a < row_end; a++) {
    pixel_blur(pic, tmp, a, row);
  }

  free(args);
}

static void parallel_sector_blur(struct picture *pic) {
  // make temporary copy of picture to work from
  struct picture tmp;
  tmp.img = copy_image(pic->img);
  tmp.width = pic->width;
  tmp.height = pic->height;  

  int max_sector_number = 8;

  threadpool thpool = thpool_init(max_sector_number);

  int width_sector_size;
  int heigth_sector_size;
  if (tmp.width >= tmp.height) {
    heigth_sector_size = max(tmp.height / 2, 1);
    width_sector_size = max(tmp.width / 4, 1);
  } else {
    heigth_sector_size = max(tmp.height / 4, 1);
    width_sector_size = max(tmp.width / 2, 1);
  }

  // iterate over each sector in the picture (ignoring boundary pixels)
  for (int a = 1; a < tmp.width - 1; a += width_sector_size) {
    for (int b = 1; b < tmp.height - 1; b += heigth_sector_size) {
      struct sector_blurring_task_args *params = (struct sector_blurring_task_args*) malloc(sizeof(struct sector_blurring_task_args)); 
      params->pic = pic;
      params->tmp = &tmp;
      params->width_start = a;
      params->heigth_start = b;
      params->width_sector_size = width_sector_size;
      params->heigth_sector_size = heigth_sector_size;
      
      thpool_add_work(thpool, (void *) sector_blurring_task, (void *) params);
    }
  }

  thpool_wait(thpool);
  thpool_destroy(thpool);  

  // temporary picture clean-up
  clear_picture(&tmp);  
}

static void sector_blurring_task(void *args_ptr) {
  struct sector_blurring_task_args *args = (struct sector_blurring_task_args *) args_ptr;
  struct picture *pic = (struct picture *) args->pic;
  struct picture *tmp = (struct picture *) args->tmp;
  int width_start = (int) args->width_start;
  int heigth_start = (int) args->heigth_start;
  int width_sector_size = (int) args->width_sector_size;
  int heigth_sector_size = (int) args->heigth_sector_size;

  for (int a = width_start; a < width_start + width_sector_size && a < tmp->width - 1; a++) {
    for (int b = heigth_start; b < heigth_start + heigth_sector_size && b < tmp->height - 1; b++) {
      pixel_blur(pic, tmp, a, b);
    }
  }

  free(args);
}

static void pixel_blur(struct picture *pic, struct picture *tmp, int a, int b) {
  // set-up a local pixel on the stack
  struct pixel rgb;  
  int sum_red = 0;
  int sum_green = 0;
  int sum_blue = 0;
  
  // check the surrounding pixel region
  for(int n = -1; n <= 1; n++){
    for(int m = -1; m <= 1; m++){
      rgb = get_pixel(tmp, a+n, b+m);
      sum_red += rgb.red;
      sum_green += rgb.green;
      sum_blue += rgb.blue;
    }
  }

  // compute average pixel RGB value
  rgb.red = sum_red / BLUR_REGION_SIZE;
  rgb.green = sum_green / BLUR_REGION_SIZE;
  rgb.blue = sum_blue / BLUR_REGION_SIZE;
  // set pixel to region average RBG value
  set_pixel(pic, a, b, &rgb);
}

static long long get_curr_time() {
  struct timeval time;
  gettimeofday(&time, NULL);
  return time.tv_sec * 1000LL + time.tv_usec / 1000; 
}

/* wrapper for the parallel_blur_picture function in PicProcess.c */
static void parallel_blur_picture_wrapped (struct picture *pic) {
  parallel_blur_picture(pic);

}

/* wrapper for the blur_picture function in PicProcess.c */
static void blur_picture_wrapped(struct picture *pic) {
  blur_picture(pic);
}