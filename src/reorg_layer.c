#include "reorg_layer.h"
#include "cuda.h"
#include "blas.h"
#include <stdio.h>


/**
 * 构建reorg层
 * reorg层在yolov2的第26层，第25层的输出26 × 26 × 512，reshape后为 13 × 13 × 2048;
 * @param batch 一个batch中包含图片的张数
 * @param w 输入图片的宽度
 * @param h 输入图片的高度
 * @param c 输入图片的通道数
 * @param stride 步幅，在yolo v2中为2
 * @param reverse 在yolo v2中为0
 * @param flatten 在yolo v2中为0
 * @param extra 在yolo v2中为0
 * @return
 */
layer make_reorg_layer(int batch, int w, int h, int c, int stride, int reverse, int flatten, int extra)
{
    layer l = {0};
    l.type = REORG; // 层类别
    l.batch = batch;
    l.stride = stride;  // 图像卷积步幅
    l.extra = extra;
    l.h = h;  // 输入图片的高度
    l.w = w;  // 输入图片的宽度
    l.c = c;  // 输入图片的通道数
    l.flatten = flatten;
    if(reverse){
        l.out_w = w*stride;
        l.out_h = h*stride;
        l.out_c = c/(stride*stride);
    }else{
        l.out_w = w/stride;
        l.out_h = h/stride;
        l.out_c = c*(stride*stride);
    }
    l.reverse = reverse;

    l.outputs = l.out_h * l.out_w * l.out_c;
    l.inputs = h*w*c;
    if(l.extra){
        l.out_w = l.out_h = l.out_c = 0;
        l.outputs = l.inputs + l.extra;
    }

    if(extra){
        fprintf(stderr, "reorg              %4d   ->  %4d\n",  l.inputs, l.outputs);
    } else {
        fprintf(stderr, "reorg              /%2d  %4d x%4d x%4d   ->  %4d x%4d x%4d\n",  stride, w, h, c, l.out_w, l.out_h, l.out_c);
    }
    int output_size = l.outputs * batch;
    l.output =  calloc(output_size, sizeof(float));
    l.delta =   calloc(output_size, sizeof(float));

    l.forward = forward_reorg_layer;
    l.backward = backward_reorg_layer;
#ifdef GPU
    l.forward_gpu = forward_reorg_layer_gpu;
    l.backward_gpu = backward_reorg_layer_gpu;

    l.output_gpu  = cuda_make_array(l.output, output_size);
    l.delta_gpu   = cuda_make_array(l.delta, output_size);
#endif
    return l;
}

void resize_reorg_layer(layer *l, int w, int h)
{
    int stride = l->stride;
    int c = l->c;

    l->h = h;
    l->w = w;

    if(l->reverse){
        l->out_w = w*stride;
        l->out_h = h*stride;
        l->out_c = c/(stride*stride);
    }else{
        l->out_w = w/stride;
        l->out_h = h/stride;
        l->out_c = c*(stride*stride);
    }

    l->outputs = l->out_h * l->out_w * l->out_c;
    l->inputs = l->outputs;
    int output_size = l->outputs * l->batch;

    l->output = realloc(l->output, output_size * sizeof(float));
    l->delta = realloc(l->delta, output_size * sizeof(float));

#ifdef GPU
    cuda_free(l->output_gpu);
    cuda_free(l->delta_gpu);
    l->output_gpu  = cuda_make_array(l->output, output_size);
    l->delta_gpu   = cuda_make_array(l->delta,  output_size);
#endif
}

void forward_reorg_layer(const layer l, network net)
{
    int i;
    if(l.flatten){
        memcpy(l.output, net.input, l.outputs*l.batch*sizeof(float));
        if(l.reverse){
            flatten(l.output, l.w*l.h, l.c, l.batch, 0);
        }else{
            flatten(l.output, l.w*l.h, l.c, l.batch, 1);
        }
    } else if (l.extra) {
        for(i = 0; i < l.batch; ++i){
            copy_cpu(l.inputs, net.input + i*l.inputs, 1, l.output + i*l.outputs, 1);
        }
    } else if (l.reverse){
        reorg_cpu(net.input, l.w, l.h, l.c, l.batch, l.stride, 1, l.output);
    } else {
        reorg_cpu(net.input, l.w, l.h, l.c, l.batch, l.stride, 0, l.output);
    }
}

void backward_reorg_layer(const layer l, network net)
{
    int i;
    if(l.flatten){
        memcpy(net.delta, l.delta, l.outputs*l.batch*sizeof(float));
        if(l.reverse){
            flatten(net.delta, l.w*l.h, l.c, l.batch, 1);
        }else{
            flatten(net.delta, l.w*l.h, l.c, l.batch, 0);
        }
    } else if(l.reverse){
        reorg_cpu(l.delta, l.w, l.h, l.c, l.batch, l.stride, 0, net.delta);
    } else if (l.extra) {
        for(i = 0; i < l.batch; ++i){
            copy_cpu(l.inputs, l.delta + i*l.outputs, 1, net.delta + i*l.inputs, 1);
        }
    }else{
        reorg_cpu(l.delta, l.w, l.h, l.c, l.batch, l.stride, 1, net.delta);
    }
}

#ifdef GPU
void forward_reorg_layer_gpu(layer l, network net)
{
    int i;
    if(l.flatten){
        if(l.reverse){
            flatten_ongpu(net.input_gpu, l.w*l.h, l.c, l.batch, 0, l.output_gpu);
        }else{
            flatten_ongpu(net.input_gpu, l.w*l.h, l.c, l.batch, 1, l.output_gpu);
        }
    } else if (l.extra) {
        for(i = 0; i < l.batch; ++i){
            copy_ongpu(l.inputs, net.input_gpu + i*l.inputs, 1, l.output_gpu + i*l.outputs, 1);
        }
    } else if (l.reverse) {
        reorg_ongpu(net.input_gpu, l.w, l.h, l.c, l.batch, l.stride, 1, l.output_gpu);
    }else {
        reorg_ongpu(net.input_gpu, l.w, l.h, l.c, l.batch, l.stride, 0, l.output_gpu);
    }
}

void backward_reorg_layer_gpu(layer l, network net)
{
    if(l.flatten){
        if(l.reverse){
            flatten_ongpu(l.delta_gpu, l.w*l.h, l.c, l.batch, 1, net.delta_gpu);
        }else{
            flatten_ongpu(l.delta_gpu, l.w*l.h, l.c, l.batch, 0, net.delta_gpu);
        }
    } else if (l.extra) {
        int i;
        for(i = 0; i < l.batch; ++i){
            copy_ongpu(l.inputs, l.delta_gpu + i*l.outputs, 1, net.delta_gpu + i*l.inputs, 1);
        }
    } else if(l.reverse){
        reorg_ongpu(l.delta_gpu, l.w, l.h, l.c, l.batch, l.stride, 0, net.delta_gpu);
    } else {
        reorg_ongpu(l.delta_gpu, l.w, l.h, l.c, l.batch, l.stride, 1, net.delta_gpu);
    }
}
#endif
