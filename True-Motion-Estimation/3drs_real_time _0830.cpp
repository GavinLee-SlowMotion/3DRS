#include <opencv2/opencv.hpp>

#include <math.h> 
#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>

#include <chrono>
#include <boost/format.hpp>
#include <ratio>

#include <string.h>
#include <thread>

#include <smmintrin.h>


using namespace std;
using namespace cv;

int BLOCKSIZE = 8;
int DISTANCE_THEROSHOLD = 40;

float get_max(float *input, int size)
{
    float max_sad = -1.;
    for (int i = 0; i < size; i++)
    {
        if ( *(input+i) > max_sad )
        {
            max_sad = *(input + i);
        }
    }
    return max_sad;
}

//motion from src to dist
//point(x,y) means (col, row)
int get_sad(uchar *src, uchar *dist, int dx, int dy, int sx, int sy, int col)
{
    int block_size(BLOCKSIZE);
    sx -= block_size/2; sy -= block_size/2;
    dx -= block_size/2; dy -= block_size/2;

    __m128i s0, s1, s2;
    s2 = _mm_setzero_si128();

    uchar *pCur, *pRef;
    pCur = (dist + dy*col + dx);
    pRef = (src + sy*col + sx);

    int sad_test(0);
    for (int row = 0; row < block_size; row++)
    {
        s0 = _mm_loadu_si128((__m128i*) pRef);
        s1 = _mm_loadu_si128((__m128i*) pCur);
        s2 = _mm_sad_epu8(s0, s1);
        sad_test += int(*((uint16_t*)&s2));

        pCur += col;
        pRef += col;
    }
    return sad_test;
}

inline float norm(int *point)
{
    return sqrt( pow(float(*(point) ), 2) + pow(float( *(point + 1) ),2) );
}


//point(x,y) means (col, row)
void oaat(uchar *src, uchar *dist, int *output, int *center, int *block_center, int searching_area, int *updates, int src_rows, int src_cols)
{
    int block_size(BLOCKSIZE), radius(block_size/2);
    output[0] = *(block_center);
    output[1] = *(block_center+1);

    while (true)
    {
        int cs[6] = { *(block_center) + *(updates)*block_size,      *(block_center + 1) + *(updates + 1)*block_size,
                      *(block_center) + *(updates + 2)*block_size,  *(block_center + 1) + *(updates + 3)*block_size,
                      *(block_center) + *(updates + 4)*block_size,  *(block_center + 1) + *(updates + 5)*block_size,
        };

		//printf("block.center = %d,%d\n",*(block_center),*(block_center+1));
        int min_SAD = 100000;
        int update[2] = {0,0};
        int decision(-1);
        for (int i = 0; i < 3; i++) //3 candidate
        {
            //cs[i*2] -> x, cs[i*2+1] -> y
            if (cs[i*2] < radius or cs[i*2+1] < radius or cs[i*2] > src_cols - radius or cs[i*2+1] > src_rows - radius)
                continue;

            int sad = get_sad(src, dist, *center, *(center+1), cs[i*2], cs[i*2+1], src_cols);
            if (sad < min_SAD)
            {
                min_SAD = sad;
                update[0] = cs[i*2];
                update[1] = cs[i*2 + 1];
                decision = i;
            }

            //if choose center of 3 point, then it should be considered as converged	-->	Point(0,0)
            if (sad < min_SAD + 40. and i == 1)
            {
                output[0] = cs[i*2];
                output[1] = cs[i*2 + 1];
                return;
            }
        }

        //Point offset(update - center);
        int offset[2] = {update[0] - *center, update[1] - *(center+1)};
		//printf("offset = %d,%d\n",offset[0],offset[1]);

        if (abs(offset[0]) > searching_area or abs(offset[1]) > searching_area)
        {
            output[0] = update[0];
            output[1] = update[1];
            break;
        }

        if (update[0] == *block_center and update[1] == *(block_center+1))
        {
            output[0] = update[0];
            output[1] = update[1];
            break;
        }

		//printf("update = %d,%d\n",update[0],update[1]);
        *block_center = update[0];
        *(block_center + 1) = update[1];
    }
}


//cal the round block score (8 block)
void cal_block_score(int ** array_map, int candidate)
{
	//sort --> x
	for (int i =0; i < candidate-1; i++)
	{
		if(array_map[i][1] > array_map[i+1][1])
		{
			for (int j = 0; j<4; j++)
			{
				int temp = array_map[i][j];
				array_map[i][j] = array_map[i+1][j];
				array_map[i+1][j] = temp;
			}

		}
	}
	
	//add socre
	for (int i =0; i < candidate; i++)
	{
		array_map[i][3] = fabs(i-(candidate)/2);
	}

	//sort --> y
	for (int i =0; i < candidate-1; i++)
	{
		if(array_map[i][2] > array_map[i+1][2])
		{
			for (int j = 0; j<4; j++)
			{
				int temp = array_map[i][j];
				array_map[i][j] = array_map[i+1][j];
				array_map[i+1][j] = temp;
			}

		}
	}

	//add socre
	for (int i =0; i < candidate; i++)
	{
		array_map[i][3] += fabs(i-(candidate)/2);
	}

	//get the vector of the max socre
	for (int i =0; i < candidate-1; i++)
	{
		if(array_map[i][3] > array_map[i+1][3])
		{
			for (int j = 0; j<4; j++)
			{
				int temp = array_map[i][j];
				array_map[i][j] = array_map[i+1][j];
				array_map[i+1][j] = temp;
			}

		}
	}

}


//pmm[y,x]
void MV_without_abnormal(int *pmm, int src_rows, int src_cols)
{
    int block_size(BLOCKSIZE);
    int searching_area(block_size*block_size), radius(block_size/2);
    int block_row(src_rows/block_size), block_col(src_cols/block_size);

	for (int r = 0; r < block_row; r++)
	{
		for (int c = 0; c < block_col; c++)
		{
			//frame boundary
			if (r == 0 or c == 0 or c == block_col-1 or r == block_row-1)
			{

				//round 8 block --> 
				int cs[16] = {-1,-1,-1,0,-1,1,0,-1,0,1,1,-1,1,0,1,1};
				int candidate = 0;

				//init 								
				int **array_map = new int*[8]; //[i][x][y][socre]
				for (int i=0;i<8;i++)
				{
					array_map[i] = new int[4];
				}
		
				//3*3 filter and assign value	
				for (int i=0; i<8;i++)
				{
					int block_r = r+cs[i*2];
					int block_c = c+cs[i*2+1];

					if (block_r < 0 or block_r >= block_row or block_c < 0 or block_c >= block_col)
					{
						//cout << i << endl;
						continue;
					}	
					else
					{
						//assign value	
						array_map[candidate][0] = candidate;					
						array_map[candidate][1] = *(pmm + (block_r*block_col*2 + block_c*2 + 1));
						array_map[candidate][2] = *(pmm + (block_r*block_col*2 + block_c*2 + 0));
						array_map[candidate][3] = 0;					
						
						candidate++;
					}

				}

				//cal the round block score (8 block)
				cal_block_score(array_map, candidate);	

				//get motion vector of the center block 
				int c_x = *(pmm + (r*block_col*2 + c*2 + 1));
				int c_y = *(pmm + (r*block_col*2 + c*2 + 0));

				//cal the distance of ed
				int distance = fabs(c_x-array_map[0][1])+fabs(c_y-array_map[0][2]);

				//judgment
				if(distance > 10)
				{
					*(pmm + (r*block_col*2 + c*2 + 1)) = array_map[0][1];
					*(pmm + (r*block_col*2 + c*2 + 0)) = array_map[0][2];
				}
				cout << distance <<endl;

				delete [] array_map;

			}
			else
			{
				//round 8 block --> 
				int cs[16] = {-1,-1,-1,0,-1,1,0,-1,0,1,1,-1,1,0,1,1};
				int candidate = 0;

				//init 								
				int **array_map = new int*[8]; //[i][x][y][socre]
				for (int i=0;i<8;i++)
				{
					array_map[i] = new int[4];
				}
		
				//3*3 filter and assign value	
				for (int i=0; i<8;i++)
				{
					int block_r = r+cs[i*2];
					int block_c = c+cs[i*2+1];
					
					//assign value	
					array_map[candidate][0] = candidate;					
					array_map[candidate][1] = *(pmm + (block_r*block_col*2 + block_c*2 + 1));
					array_map[candidate][2] = *(pmm + (block_r*block_col*2 + block_c*2 + 0));
					array_map[candidate][3] = 0;					
					
					candidate++;
				}

				//cal the round block score (8 block)
				cal_block_score(array_map, candidate);	

				//get motion vector of the center block 
				int c_x = *(pmm + (r*block_col*2 + c*2 + 1));
				int c_y = *(pmm + (r*block_col*2 + c*2 + 0));

				//cal the distance of ed
				int distance = fabs(c_x-array_map[0][1])+fabs(c_y-array_map[0][2]);

				//judgment
				if(distance > DISTANCE_THEROSHOLD)
				{
					*(pmm + (r*block_col*2 + c*2 + 1)) = array_map[0][1];
					*(pmm + (r*block_col*2 + c*2 + 0)) = array_map[0][2];
				}
				cout << distance <<endl;

				delete [] array_map;

			}
		}
	}
}




void tdrs_thread(uchar *dsrc, uchar *ddist, int *plmm, int *pmm, int src_rows, int src_cols, int cur_thread, int num_thread)
{
    int block_size(BLOCKSIZE);
    int searching_area(block_size*block_size), radius(block_size/2);
    int block_row(src_rows/block_size), block_col(src_cols/block_size);

 	//cout << "thread num is " << cur_thread << " working" << endl;

    for (int r = cur_thread; r < block_row; r += num_thread)
    {
        for (int c = 0; c < block_col; c++)
        {
            int center[2] = {c*block_size + radius,  r*block_size + radius};

            //boundary, use one-at-a-time
            if (r == 0 or c == 0 or c == block_col - 1)
            {
				//printf("cur_t = %d, r=%d\n",cur_thread,r);
                //horizontal search
                int updates_h[6] = {0,-1,0,0,0,1};      //vector<Point> updates_h {Point(-1,0), Point(0,0), Point(1,0)};
                int block_center[2] = {0};
                int init_block_center[2] = {center[0], center[1]};
                oaat(dsrc, ddist, block_center, center, init_block_center, searching_area, updates_h, src_rows, src_cols);
                //vertical search
                int updates_v[6] = {-1,0,0,0,1,0};     //vector<Point> updates_v {Point(0,-1), Point(0,0), Point(0,1)};
                int from_point[2] = {0};
                oaat(dsrc, ddist, from_point, center, block_center, searching_area, updates_v, src_rows, src_cols);

                *(pmm + (r*block_col*2 + c*2 + 0)) = (center[0] - from_point[0]);  //what I trying to do : motion_map[r][c] = (center - from_point);
                *(pmm + (r*block_col*2 + c*2 + 1)) = (center[1] - from_point[1]);
            }
            // 3d recursive searching
            else
            {
                //magic number from paper
                int p(9);
                int updates[18] = {0,0, 1,0, -1,0, 2,0, -2,0 ,0,1, 0,-1, 0,-3, 0,3 };

                //initial estimation is early cauculated value
				//fix big bug!!!
                int Da_current[2] = { *(pmm+((r-1)*block_col*2 + (c-1)*2 + 0)), *(pmm + ( (r-1)*block_col*2 + (c-1)*2 + 1))}; //motion_map[r-1][c-1]	//Sa
                int Db_current[2] = { *(pmm+((r-1)*block_col*2 + (c+1)*2 + 0)), *(pmm + ( (r-1)*block_col*2 + (c+1)*2 + 1))}; //motion_map[r-1][c+1]	//Sb

                //inital CAs
                int Da_previous[2] = {0,0};
                int Db_previous[2] = {0,0};

                //if there is CAs
                if (c > 2 and r < block_row -2 and c < block_row -2)
                {
                    Da_previous[0] =  *(plmm + ((r+2)*block_col*2 + (c+2)*2 + 0)); //last_motion[r+2][c+2]	//Ta
                    Da_previous[1] =  *(plmm + ((r+2)*block_col*2 + (c+2)*2 + 1));
                    Db_previous[0] =  *(plmm + ((r+2)*block_col*2 + (c-2)*2 + 0)); //last_motion[r+2][c-2]	//Tb
                    Db_previous[1] =  *(plmm + ((r+2)*block_col*2 + (c-2)*2 + 1));
                }

                int block_cnt_a(r*c), block_cnt_b(r*c+2);
				//printf("block_cnt = %d,%d\n",block_cnt_a,block_cnt_b);
                float SAD_a(100000.), SAD_b(100000.);
                int not_update_a(0), not_update_b(0);

                while (true)
                {
                    int candidate_a[8] = {0};        int candidate_b[8] = {0};
                    float candidate_sad_a[4] = {0};  float candidate_sad_b[4] = {0};
                    bool candidate_index_a[4] = {0};  bool candidate_index_b[4] = {0};

                    int update_a[2] = {updates[(block_cnt_a %p)*2], updates[(block_cnt_a %p)*2 + 1]};
                    int update_b[2] = {updates[(block_cnt_b %p)*2], updates[(block_cnt_b %p)*2 + 1]};
					//printf("%d,block_cnt %% p = %d\n",block_cnt_a,(block_cnt_a %p)*2);
                    block_cnt_a++; block_cnt_b++;

                    //inital candidate set, 1st : ACS, 2nd : CAs, 3th : 0
                    int cs_a[8] = {Da_current[0], Da_current[1],
                                   Da_current[0] + update_a[0], Da_current[1] + update_a[1],
                                   Da_previous[0], Da_previous[1],
                                   0,0};
                    int cs_b[8] = {Db_current[0], Db_current[1],
                                   Db_current[0] + update_b[0], Db_current[1] + update_b[1],
                                   Db_previous[0], Db_previous[1],
                                   0,0};

                    //get SAD from each candidate, there are 4 candidate each time
                    for (int index = 0; index < 4; index++)
                    {
                        bool out_of_boundary_a(false), out_of_boundary_b(false);
                        int eval_center_a[2] = {center[0] - cs_a[index*2],  center[1] - cs_a[index*2 + 1]};
                        int eval_center_b[2] = {center[0] - cs_b[index*2],  center[1] - cs_b[index*2 + 1]};

                        if (eval_center_a[0] < radius or eval_center_a[1] < radius or eval_center_a[0] > src_cols - radius or eval_center_a[1] > src_rows - radius)
                            out_of_boundary_a = true;
                        if (eval_center_b[0] < radius or eval_center_b[1] < radius or eval_center_b[0] > src_cols - radius or eval_center_b[1] > src_rows - radius)
                            out_of_boundary_b = true;
                        if (out_of_boundary_a and out_of_boundary_b)
                            continue;

                        if (not out_of_boundary_a)
                        {
                            candidate_a[index*2] = cs_a[index*2];
                            candidate_a[index*2 + 1] = cs_a[index*2 + 1];
                            candidate_sad_a[index] = get_sad(dsrc, ddist, center[0], center[1], eval_center_a[0], eval_center_a[1] , src_cols);
                            candidate_index_a[index] = 1;
                        }

                        if (not out_of_boundary_b)
                        {
                            candidate_b[index*2] = cs_b[index*2];
                            candidate_b[index*2 + 1] = cs_b[index*2 + 1];
                            candidate_sad_b[index] = get_sad(dsrc, ddist, center[0], center[1], eval_center_b[0], eval_center_b[1] , src_cols);
                            candidate_index_b[index] = 1;
                        }
                    }

                    //compute penalty from each candidate
                    float min_sad_a(100000.), min_sad_b(100000.);
                    int tmp_update_a[2] = {0};
                    int tmp_update_b[2] = {0};

                    float max_sad_a = get_max(candidate_sad_a, 4);
                    float max_sad_b = get_max(candidate_sad_b, 4);

                    //compute estimator a
                    for (int i = 0; i < 4; i++)
                    {
                        if (!candidate_index_a[i])
                            continue;
                        float current_sad = candidate_sad_a[i];
                        float penalty(0);
                        switch (i)
                        {
                            case 0:
                                penalty = 0.;
                                break;
                            case 1:
                                penalty = 0.004 * max_sad_a * norm(update_a);
                                break;
                            case 2:
                                penalty = 0.008 * max_sad_a;
                                break;
                            case 3:
                                penalty = 0.016 * max_sad_a;
                                break;
                        }

                        current_sad += penalty;
                        if (min_sad_a > current_sad)
                        {
                            min_sad_a = current_sad;
                            tmp_update_a[0] = candidate_a[i*2];
                            tmp_update_a[1] = candidate_a[i*2+1];
                        }
                        //prefer 0 in the case of same SAD
                        else if (min_sad_a + 40. > current_sad and candidate_a[i*2] == 0 and candidate_a[i*2+1] == 0)
                        {
                            tmp_update_a[0] = 0;
                            tmp_update_a[1] = 0;
                        }
                    }

                    //compute estimator b
                    for (int i = 0; i < 4; i++)
                    {
                        if (!candidate_index_b[i])
                            continue;
                        float current_sad = candidate_sad_b[i];
                        float penalty(0);
                        switch (i)
                        {
                            case 0:
                                penalty = 0.;
                                break;
                            case 1:
                                penalty = 0.004 * max_sad_b * norm(update_b);
                                break;
                            case 2:
                                penalty = 0.008 * max_sad_b;
                                break;
                            case 3:
                                penalty = 0.016 * max_sad_b;
                                break;
                        }
                        current_sad += penalty;
                        if (min_sad_b > current_sad)
                        {
                            min_sad_b = current_sad;
                            tmp_update_b[0] = candidate_b[i*2];
                            tmp_update_b[1] = candidate_b[i*2+1];
                        }
                        //prefer 0 in the case of same SAD
                        else if (min_sad_b + 40.  > current_sad and candidate_b[i*2] == 0 and candidate_b[i*2+1] == 0)
                        {
                            tmp_update_b[0] = 0;
                            tmp_update_b[1] = 0;
                        }
                    }

                    //update, if not, counter + 1
                    if (min_sad_a < SAD_a)
                    {
                        SAD_a = min_sad_a;
                        Da_current[0] = tmp_update_a[0];
                        Da_current[1] = tmp_update_a[1];
                        not_update_a = 0;
                    }
                    else
                        not_update_a += 1;

                    if (min_sad_b < SAD_b)
                    {
                        SAD_b = min_sad_b;
                        Db_current[0] = tmp_update_b[0];
                        Db_current[1] = tmp_update_b[1];
                        not_update_b = 0;
                    }
                    else
                        not_update_b += 1;

                    //from paper p373, imporve 2nd withdraw
                    float threshold = 5;
                    if (SAD_a > SAD_b + threshold)
                    {
                        SAD_a = SAD_b;
                        Da_current[0] = Db_current[0];
                        Da_current[1] = Db_current[1];
                        not_update_a = 0;
                    }
                    if (SAD_b > SAD_a + threshold)
                    {
                        SAD_b = SAD_a;
                        Db_current[0] = Da_current[0];
                        Db_current[1] = Da_current[1];
                        not_update_b = 0;
                    }

                    //break if any estiminator converge
                    int check_converge = 2;
                    if (not_update_a > check_converge or not_update_b > check_converge)
                    {
                        break;
                    }
                    //break if out of searching area
                    if (abs(Da_current[0]) > searching_area or abs(Da_current[1]) > searching_area or abs(Db_current[0]) > searching_area or abs(Db_current[1]) > searching_area)
                    {
                        break;
                    }
                }

                if (SAD_a <= SAD_b)
                {
                    *(pmm + (r*block_col*2 + c*2)) = Da_current[0];
                    *(pmm + (r*block_col*2 + c*2 + 1)) = Da_current[1]; //motion_map[r][c] = Da_current
                }
                else
                {
                    *(pmm + (r*block_col*2 + c*2)) = Db_current[0];
                    *(pmm + (r*block_col*2 + c*2 + 1)) = Db_current[1]; //motion_map[r][c] = Db_current
                }
            }

        }
    }
}



//imread generate a continous matrix
void tdrs(Mat &src, Mat &dist, Mat &draw, int *last_motion)
{
    int block_size(BLOCKSIZE), radius(block_size/2);
    int row(src.rows), col(src.cols);
    int block_row(row/block_size), block_col(col/block_size);

    int img_size = row * col;
    uchar *dsrc, *ddist;
    dsrc = new uchar[img_size];
    memcpy(dsrc, src.data, img_size*sizeof(uchar));
    ddist = new uchar[img_size];
    memcpy(ddist, dist.data, img_size*sizeof(uchar));


    //initial zero motion map
    int size = block_row * block_col * 2;
    int *motion_map = new int[size]();

    int *pmm, *plmm;
    pmm = new int[size];
    memcpy(pmm, motion_map, size*sizeof(int));
    plmm = new int[size];
    memcpy(plmm, last_motion, size*sizeof(int));

    int total_thread = 1;
    thread ts[total_thread];
    for (int i = 0; i < total_thread; i++)
        ts[i] = thread(tdrs_thread, dsrc, ddist, plmm, pmm, src.rows, src.cols, i, total_thread);
    for (int i = 0; i < total_thread; i++)
        ts[i].join();

    //update last_motion for next frame
    memcpy(last_motion, pmm, size*sizeof(int));

    //free memory
    delete [] pmm; delete [] plmm; delete [] dsrc; delete [] ddist; delete [] motion_map;

	//draw the arrowed line
    for (int r = 0; r < block_row; r++)
    {
        for (int c = 0; c < block_col; c++)
        {
            if ( *(last_motion + r*block_col*2 + c*2) != 0 or *(last_motion + r*block_col*2 + c*2 + 1) != 0)
            {
                Point motion(*(last_motion + r*block_col*2 + c*2) , *(last_motion + r*block_col*2 + c*2 + 1));
				//printf("%d,%d\n",motion.x,motion.y);
                Point center(c*block_size + radius, r*block_size + radius);
				//printf("%d,%d\n",center.x,center.y);
                Point from = center - motion;
                arrowedLine(draw, from, center, Scalar(0,255,0));
            }
        }
    }
	
} 



//imread generate a continous matrix
void tdrs_MC(Mat &src, Mat &dist, Mat &draw, Mat &src_image, Mat &IF_image,int *last_motion)
{
    int block_size(BLOCKSIZE), radius(block_size/2);
    int row(src.rows), col(src.cols);
    int block_row(row/block_size), block_col(col/block_size);
    int img_size = row * col;

    uchar *dsrc, *ddist;
    dsrc = new uchar[img_size];
    memcpy(dsrc, src.data, img_size*sizeof(uchar));
    ddist = new uchar[img_size];
    memcpy(ddist, dist.data, img_size*sizeof(uchar));


    //initial zero motion map
    int size = block_row * block_col * 2;
    int *motion_map = new int[size]();

    int *pmm, *plmm;
    pmm = new int[size];
    memcpy(pmm, motion_map, size*sizeof(int));
    plmm = new int[size];
    memcpy(plmm, last_motion, size*sizeof(int));

    int total_thread = 1;
    thread ts[total_thread];
    for (int i = 0; i < total_thread; i++)
        ts[i] = thread(tdrs_thread, dsrc, ddist, plmm, pmm, src.rows, src.cols, i, total_thread);
    for (int i = 0; i < total_thread; i++)
        ts[i].join();

	//motion vector excluding outliers
	MV_without_abnormal(pmm,row,col);

    //update last_motion for next frame
    memcpy(last_motion, pmm, size*sizeof(int));

    //free memory
    delete [] pmm; delete [] plmm; delete [] dsrc; delete [] ddist; delete [] motion_map;

	//draw the arrowed line
/*
    for (int r = 0; r < block_row; r++)
    {
        for (int c = 0; c < block_col; c++)
        {
            if ( *(last_motion + r*block_col*2 + c*2) != 0 or *(last_motion + r*block_col*2 + c*2 + 1) != 0)
            {
                Point motion(*(last_motion + r*block_col*2 + c*2) , *(last_motion + r*block_col*2 + c*2 + 1));
				//printf("%d,%d\n",motion.x,motion.y);
                Point center(c*block_size + radius, r*block_size + radius);
				//printf("%d,%d\n",center.x,center.y);
                Point from = center - motion;
                arrowedLine(draw, from, center, Scalar(0,255,0));
            }
        }
    }
*/	
/*	
	//Forward MC
	for (int r = 0; r< block_row; r++)
	{
		for (int c = 0; c< block_col;c++)
		{
			if (*(last_motion + r*block_col*2 + c*2) != 0 or *(last_motion + r*block_col*2 + c*2 + 1) != 0)	
			{
				//fix this bug!!!! last_motion --> (y,x)
				Point motion(*(last_motion + r*block_col*2 + c*2+1) , *(last_motion + r*block_col*2 + c*2));
				motion.x = int(motion.x*0.5);
				motion.y = int(motion.y*0.5);
				//cout << "motion.x/y is " << motion << endl;
				for (int a = 0; a< block_size; a++)
				{
					for (int b = 0; b < block_size; b++)
					{
						Point loc_dist(r*block_size+a+motion.x,c*block_size+b+motion.y);
						if ((loc_dist.x <0) or (loc_dist.x > row-1) or (loc_dist.y <0 ) or (loc_dist.y > col-1))
						{
							//cout << "out of the edge, x is " << loc_dist.x << "and y is " << loc_dist.y << endl;
							continue;
						}
						else
						{
							Vec3b src_pix = src_image.at<Vec3b>(r*block_size+a,c*block_size+b);
							//cout << src_pix << endl;
							IF_image.at<Vec3b>(r*block_size+a+motion.x,c*block_size+b+motion.y) = src_pix;
							//cout << IF_image.at<Vec3b>(block_row*block_size+a+motion.x,block_col*block_size+b+motion.y) << endl;
						}
 
					}
				}
			}	
		}
	}
*/	
	
	Mat next_img = IF_image.clone();
	//Both ward
	for (int r = 0; r< block_row; r++)
	{
		for (int c = 0; c< block_col;c++)
		{
			if (*(last_motion + r*block_col*2 + c*2) != 0 or *(last_motion + r*block_col*2 + c*2 + 1) != 0)	
			{
				//fix this bug!!!! last_motion --> (y,x)
				Point motion(*(last_motion + r*block_col*2 + c*2+1) , *(last_motion + r*block_col*2 + c*2));
				motion.x = int(motion.x*0.5);
				motion.y = int(motion.y*0.5);
				//cout << "motion.x/y is " << motion << endl;
				for (int a = 0; a< block_size; a++)
				{
					for (int b = 0; b < block_size; b++)
					{
						Point loc_dist_prev(r*block_size+a-motion.x,c*block_size+b-motion.y);
						Point loc_dist_next(r*block_size+a+motion.x,c*block_size+b+motion.y);
						if ((loc_dist_prev.x <0) or (loc_dist_prev.x > row-1) or (loc_dist_prev.y <0 ) or (loc_dist_prev.y > col-1) or (loc_dist_next.x <0) or (loc_dist_next.x > row-1) or (loc_dist_next.y <0 ) or (loc_dist_next.y > col-1))
						{
							//cout << loc_dist_prev.x << " " << loc_dist_prev.y << " ; " << loc_dist_next.x << " " << loc_dist_next.y << " ; " << endl;
							continue;
						}
						else
						{
							Vec3b src_pix = src_image.at<Vec3b>(r*block_size+a-motion.x,c*block_size+b-motion.y);
							Vec3b dist_pix = next_img.at<Vec3b>(r*block_size+a+motion.x,c*block_size+b+motion.y);
							for (int i = 0;i <3 ;i++)
							{
								IF_image.at<Vec3b>(r*block_size+a,c*block_size+b)[i] = int(0.5*(src_pix[i]+dist_pix[i]));			
							}
							//cout << src_pix << endl;
							//cout << IF_image.at<Vec3b>(block_row*block_size+a+motion.x,block_col*block_size+b+motion.y) << endl;
						}
 
					}
				}
			}	
		}
	}

} 



//point(x,y) means (col, row)
int main(int argc, char**argv)
{
	printf("start\n");
	string file_name = "/home/iqiyi/Desktop/ESPCN_shijie/True-Motion-Estimation/Oigin_SDBronze.mp4";
	//string file_name = "/zhangjie/bbb_sunflower_1080p_60fps_normal.mp4";
	//string file_name = "/home/iqiyi/Desktop/ESPCN_shijie/True-Motion-Estimation/movie/FOREMAN_352x288_30_orig_01.avi";

    int cnt(0), block_size(BLOCKSIZE);
    VideoCapture cap;
    long VideoTotalFrame;

    if (!cap.open(file_name))
	{
		cout << file_name << "open is fail" << endl;
		exit(1)	;
        return -1;	
	}
	else
	{
		cout << "load video " << file_name << endl;
		VideoTotalFrame = cap.get(CV_CAP_PROP_FRAME_COUNT);
		cout << "the total frame of the Video is " << VideoTotalFrame << endl;
	}	

    Mat src, dist;
    cap >> src;
	// *2 --> to save y and x of the motion
    int *motion_map = new int[src.cols/block_size * src.rows/block_size * 2]();

    chrono::duration<float, milli> dtn;
    float avg_dtn;

    VideoWriter record("record.avi", CV_FOURCC('M','J','P','G'), cap.get(CV_CAP_PROP_FPS )*2, Size(src.cols, src.rows), true);

	
    while (1)
    {
        chrono::steady_clock::time_point start = chrono::steady_clock::now();
        Mat gsrc, gdist, out, IF_img;

        cap >> dist;
		//empth --> the last frame in the video
		if (dist.empty())
		{
			break;
		}
        out = dist.clone();

        cvtColor(src, gsrc, CV_BGR2GRAY);
        cvtColor(dist, gdist, CV_BGR2GRAY);

		//debug
		//imwrite(boost::str(boost::format("./video/%04d_a.jpg") %cnt).c_str(), src);	
		//imwrite(boost::str(boost::format("./video/%04d_b.jpg") %cnt).c_str(), dist);
		//imshow("src",gsrc);
		//imshow("dist",gdist);

        //tdrs(gsrc, gdist, out, motion_map);
		IF_img = src.clone();
		tdrs_MC(gsrc, gdist, out, src, IF_img, motion_map);

		//frame reflash to the next circle
        src = dist.clone();

        chrono::steady_clock::time_point end = chrono::steady_clock::now();
        dtn = end - start;
        avg_dtn = (cnt/float(cnt+1))*avg_dtn + (dtn.count()/float(cnt+1));
        cnt++;
		//cout << "cnt is " << cnt << endl;

        string tmp = boost::str(boost::format("%2.2fms / %2.2fms")% dtn.count()  %avg_dtn );
        putText(out, tmp, cvPoint(30,30), FONT_HERSHEY_COMPLEX_SMALL, 0.8, cvScalar(0,200,0), 1, CV_AA);
		putText(IF_img, tmp, cvPoint(30,30), FONT_HERSHEY_COMPLEX_SMALL, 0.8, cvScalar(0,200,0), 1, CV_AA);

		//record the video
		record.write(IF_img);
        record.write(out);

		//save the image of the output
		//imwrite(boost::str(boost::format("./video/%04d_a.jpg") %cnt).c_str(), IF_img);
		//imwrite(boost::str(boost::format("./video/%04d_b.jpg") %cnt).c_str(), out);	

        imshow("motion", IF_img);
		//imshow("motion", out);
		//waitKey(0);

        if (char(waitKey(1)) == 'q')
            break;
    }
    delete [] motion_map;

	cout << "the process has been completed" <<endl;
    return 0;
}
