#ifndef SEND_H
#define SEND_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h> //socket
#include <arpa/inet.h>  //inet_addr
#include <unistd.h>     //write

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include "gdal.h"
#include <gdal_priv.h>
#include <gdal/gdal.h>
#include "gdal/gdal_priv.h"
#include "gdal/cpl_conv.h"



struct obj_coords
{
    float LAT;
    float LONG;
    float cl;
};


void readTiff(char*filename, double *adfGeoTransform)
{
	GDALDataset  *poDataset;
	GDALAllRegister();
	poDataset = (GDALDataset *) GDALOpen( filename, GA_ReadOnly );
	if( poDataset != NULL )
	{
		//int colms = poDataset->GetRasterXSize();
		//int rows = poDataset->GetRasterYSize();
		poDataset->GetGeoTransform( adfGeoTransform );
	}	
}

void pixel2coord(int x, int y, double &lat, double &lon, double *adfGeoTransform)
{
	
	double xoff, a, b, yoff, d, e;
	xoff = adfGeoTransform[0];
	a = adfGeoTransform[1];
	b = adfGeoTransform[2];
	yoff = adfGeoTransform[3];
	d = adfGeoTransform[4];
	e = adfGeoTransform[5];

	//printf("%f %f %f %f %f %f\n",xoff, a, b, yoff, d, e );

    lon = a * x + b * y + xoff;
    lat = d * x + e * y + yoff;

}

void fillMatrix(cv::Mat &H, float *matrix, bool show=false)
{
	double *vals = (double*) H.data;
	for(int i=0; i<9; i++) {
		vals[i] = matrix[i];
	}
	if(show)
		std::cout<<H<<"\n";
}

char *serialize_coords(struct obj_coords *c, int obj_n, int CAM_IDX)
{

    char *buffer = (char *)malloc(100000 * sizeof(char));

    struct timeval tv;
    gettimeofday(&tv, NULL);
    unsigned long long t_stamp_ms = (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;

    sprintf(buffer, "m %lld %d %d ", t_stamp_ms, CAM_IDX, obj_n);
    int i;
    for (i = 0; i < obj_n; i++)
        sprintf(buffer + strlen(buffer), "%.9f %.9f %.0f ", c[i].LAT, c[i].LONG, c[i].cl);

    //printf("%s\n", buffer);

    return buffer;
}

struct obj_coords *deserialize_coords(char *buffer, int *obj_n)
{

    int cam_id;
    unsigned long long t_stamp_ms;
    char type_of_m;

    int consumed_chars = 0;
    char shifted_chars[100000];

    sscanf(buffer, "%c %lld %d %d ", &type_of_m, &t_stamp_ms, &cam_id, obj_n);
    sprintf(shifted_chars, "%c %lld %d %d ", type_of_m, t_stamp_ms, cam_id, *obj_n);
    consumed_chars += strlen(shifted_chars);
    //printf("%c %lld %d %d\n", type_of_m, t_stamp_ms, cam_id, *obj_n);

    struct obj_coords *c = (struct obj_coords *)malloc(*obj_n * sizeof(struct obj_coords));

    int i;
    for (i = 0; i < *obj_n; i++)
    {
        sscanf(buffer + consumed_chars, "%f %f %f ", &c[i].LAT, &c[i].LONG, &c[i].cl);
        sprintf(shifted_chars, "%.9f %.9f %.0f ", c[i].LAT, c[i].LONG, c[i].cl);
        consumed_chars += strlen(shifted_chars);
        //printf("%Lf %f %f \n", c[i].LAT, c[i].LONG, c[i].cl);
    }

    return c;
}

int map_class_coco_to_voc(int coco_class)
{
    switch (coco_class)
    {
    case 0:
        return 14; //person
    case 1:
        return 1; //bicycle
    case 2:
        return 6; //car
    case 3:
        return 13; //motorkbike
    case 5:
        return 5; //bus
    }
    return -1;
}

FILE *out_file = fopen("prova_pixel.txt", "w");

void convert_coords(struct obj_coords *coords, int i, int x, int y, int detected_class,cv::Mat H, double *adfGeoTransform, int frame_nbr)
{
    double latitude, longitude;
	std::vector<cv::Point2f> x_y, ll;
    x_y.push_back(cv::Point2f(x, y));

    //transform camera pixel to map pixel
    cv::perspectiveTransform( x_y, ll, H);
    //tranform to map pixel to map gps
    pixel2coord(ll[0].x, ll[0].y, latitude,longitude, adfGeoTransform);
    printf("lat: %f, long:%f \n", latitude, longitude);
    coords[i].LAT = latitude;
    coords[i].LONG = longitude;
    coords[i].cl = map_class_coco_to_voc(detected_class);

    if(detected_class == 0)
    {
        
        struct timeval tv;
        gettimeofday(&tv, NULL);
        unsigned long long t_stamp_ms = (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;

        //fprintf(out_file, "%d %lld %d %d\n",frame_nbr, t_stamp_ms, int(ll[0].x), int(ll[0].y));
        fprintf(out_file, "%d %lld %f %f\n",frame_nbr, t_stamp_ms, coords[i].LAT, coords[i].LONG);
    
    }
    
}

void read_projection_matrix(cv::Mat &H, int &proj_matrix_read, char* path)
{
    FILE *fp;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    float* proj_matrix = (float*) malloc(9*sizeof(float));

    fp = fopen(path, "r");
    if (fp == NULL)
        exit(EXIT_FAILURE);

    int i = 0;
    while ((read = getline(&line, &len, fp)) != -1)
    {
        if (3 == sscanf(line, "%f %f %f", &proj_matrix[i*3+0], &proj_matrix[i*3+1], &proj_matrix[i*3+2]))
        {
            i++;
            proj_matrix_read = 1;
        }
    }
    free(line);
    fclose(fp);
	fillMatrix(H, proj_matrix);

    free(proj_matrix);
}

int open_socket(char *ip, int &sock, int &socket_opened)
{
    struct sockaddr_in server;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        printf("Could not create socket");
    }
    puts("Socket created");    

    server.sin_addr.s_addr = inet_addr(ip);
    server.sin_family = AF_INET;
    server.sin_port = htons(8888);

    /*connect to remote server*/
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("connect failed. Error");
        socket_opened = 0;
        return 0;
    }
    puts("Connected\n");
    socket_opened = 1;
    return 1;
}

int send_client_dummy(struct obj_coords *coords, int n_coords, int &sock, int &socket_opened, int CAM_IDX)
{
    /*serialize coords*/
    char *message = serialize_coords(coords, n_coords, CAM_IDX);

    /*open socket if not already opened*/
    if (socket_opened == 0)
    {
        int res = open_socket("127.0.0.1",sock, socket_opened);
        if(res)
            printf("Socket opened!\n");
        else
        {
            printf("Problem: socket NOT opened!\n");
            return 0;
        }
    }

    /*send message to server*/
    if (send(sock, message, strlen(message), 0) < 0)
    {
        puts("Send failed");
        socket_opened = 0;
    }

    free(message);
    //close(sock);
    return 1;
}

#endif /*SEND_H*/