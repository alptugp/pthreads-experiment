#ifndef PICLIB_H
#define PICLIB_H

#include "Picture.h"
#include "Utils.h"
#include <stdio.h>
#include <pthread.h>
  
  // picture transformation routines
  void invert_picture(struct picture *pic);
  void grayscale_picture(struct picture *pic);
  void rotate_picture(struct picture *pic, int angle);
  void flip_picture(struct picture *pic, char plane);
  void blur_picture(struct picture *pic);
  void parallel_blur_picture(struct picture *pic);
  void *task(void *args_ptr);

  struct task_args {
    struct picture *pic;
    struct picture tmp;
    int i;
    int j;
  };

#endif

