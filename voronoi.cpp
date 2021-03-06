/*
Guodong Rong

voronoi.cpp
The main program using JFA to compute Voronoi diagram

Copyright (c) 2005-2006 
School of Computing
National University of Singapore
All Rights Reserved.
*/

/*
 * 주요 FLOW
 * Display()
 * BFGSOptimization()
 * - lbfgsbminimize()
 * --- funcgrad()
 * - DrawVoronoi()
 */

#include "voronoi.h"
#include <assert.h>

extern void Energyf(cudaGraphicsResource_t grSite, real* g, real* f, int w, int h, int nsite, const cudaStream_t& stream);
// x를 VBO에 저장한다.
extern void ConvertSites(real* x, cudaGraphicsResource_t gr, int nsite, int screenw, int screenh, const cudaStream_t& stream);
extern void InitSites(real* x, float* init_sites, int stride, int* nbd, real* l, real* u, int nsite, int screenw, int screenh);

// Temperary array for site_list
float* site_list_x = NULL;
float* site_list_x_bar = NULL;
float site_perturb_step = 0;

inline void CopySite(SiteType* dst, float* src, int n) {
	for(int i = 0; i < n; i++) {
		dst[i].vertices[0].x = src[2 * i];
		dst[i].vertices[0].y = src[2 * i + 1];
	}
}

inline void CopySite(float* dst, float* src, int n) {
	memcpy(dst, src, n * 2 * sizeof(float));
}

inline void CopySite(float* dst, SiteType* src, int n) {
	for(int i = 0; i < n; i++) {
		dst[2 * i] = src[i].vertices[0].x;
		dst[2 * i + 1] = src[i].vertices[0].y;
	}
}

/******************************************************************************/
void CheckFramebufferStatus()
{
	GLenum status;
	status = (GLenum) glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
	switch(status) {
		case GL_FRAMEBUFFER_COMPLETE_EXT:
			break;
		case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
			printf("Unsupported framebuffer format\n");
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT:
			printf("Framebuffer incomplete, missing attachment\n");
			break;
		//case GL_FRAMEBUFFER_INCOMPLETE_DUPLICATE_ATTACHMENT_EXT:
		//	printf("Framebuffer incomplete, duplicate attachment\n");
		//	break;
		case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
			printf("Framebuffer incomplete, attached images must have same dimensions\n");
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
			printf("Framebuffer incomplete, attached images must have same format\n");
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT:
			printf("Framebuffer incomplete, missing draw buffer\n");
			break;
		case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT:
			printf("Framebuffer incomplete, missing read buffer\n");
			break;
		default:
			exit(0);
	}
}

// x를 VBO에 복사한 후, VBO를 이용해 Site들을 출력한다.
void DrawSites(real* x, const cudaStream_t& stream)
{
	// 점의 크기를 1픽셀로 설정
	glPointSize(1);

	// VBO를 바인딩하고, VertextPointer를 선언한다.
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, vboId);

	// 배열에 x,y 좌표를 차례로 저장하고 있어, point_num*2의 크기를 지닌다.
	ConvertSites(x, grVbo, point_num * 2, screenwidth, screenheight, stream);

	glVertexPointer(2, GL_FLOAT, 0, 0);

	// CBO를 바인딩하고, ColorPointer를 선언한다.
	glBindBuffer(GL_ARRAY_BUFFER_ARB, colorboId);
	glColorPointer(4, GL_FLOAT, 0, 0);
	
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	// VBO, CBO 값을 파라미터로 밖에서 지정한 Shader가 실행
	glDrawArrays(GL_POINTS, 0, point_num);

	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);

	// 바인딩 해제
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
}

// Function of L-BFGS-B
void funcgrad(real* x, real& f, real* g, const cudaStream_t& stream)
{
	int i,j;
	get_timestamp(start_time_func);

	//////////////////////////////////////////////
	// First pass - Render the initial sites    //
	//////////////////////////////////////////////

	// FB_objects에 Processed_Texture[0]을 반영
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, FB_objects);
	// 결과가 Processed_Texture[0]에 반영되도록 설정
	// fbo_attachments는 ColorAttachemnt 번호
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, fbo_attachments[0],
		GL_TEXTURE_RECTANGLE_NV, Processed_Texture[0], 0);
	CheckFramebufferStatus();

	// 출력할 버퍼를 선정
	glDrawBuffer(fbo_attachments[0]);
	glClearColor(-1, -1, -1, -1);
	glClear(GL_COLOR_BUFFER_BIT);

	cgGLEnableProfile(VertexProfile);
	cgGLEnableProfile(FragmentProfile);

	cgGLBindProgram(VP_DrawSites);
	cgGLBindProgram(FP_DrawSites);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(1, screenwidth+1, 1, screenheight+1);
	glViewport(1, 1, screenwidth, screenheight);

	DrawSites(x, stream);

	// glReadBuffer(fbo_attachments[0]);
	// imdebugPixelsf(0, 0, screenwidth+2, screenheight+2, GL_RGBA, "funcgrad - First Pass");

	Current_Buffer = 1; // 다음에 사용할 Texture를 선정

	/////////////////////////////////////
	// Second pass - Flood the sites   //
	/////////////////////////////////////
	cgGLBindProgram(VP_Flood);
	cgGLBindProgram(FP_Flood);

	if (VP_Flood_Size != NULL)
		cgSetParameter2f(VP_Flood_Size, screenwidth, screenheight);

	bool ExitLoop = false;
	bool SecondExit;
	int steplength;;
	SecondExit = (additional_passes==0);
	bool PassesBeforeJFA;
	PassesBeforeJFA = (additional_passes_before>0);
	if (PassesBeforeJFA)
		steplength = pow(2.0, (additional_passes_before-1));
	else
		steplength = (screenwidth>screenheight ? screenwidth : screenheight)/2;

	while (!ExitLoop)
	{
		// Pixel 단위로 값을 저장하고 있는 Processed_Texture를 VertextBuffer로 사용???
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, fbo_attachments[Current_Buffer], 
			GL_TEXTURE_RECTANGLE_NV, Processed_Texture[Current_Buffer], 0);
		CheckFramebufferStatus();
		glDrawBuffer(fbo_attachments[Current_Buffer]);

		glClearColor(-1, -1, -1, -1);
		glClear(GL_COLOR_BUFFER_BIT);

		//Bind & enable shadow map texture
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_NV, Processed_Texture[1-Current_Buffer]);
		if (VP_Flood_Steplength != NULL)
			cgSetParameter1d(VP_Flood_Steplength, steplength);

		glBegin(GL_QUADS);
			glVertex2f(1.0, 1.0);
			glVertex2f(1.0, float(screenheight+1));
			glVertex2f(float(screenwidth+1), float(screenheight+1));
			glVertex2f(float(screenwidth+1), 1.0);
		glEnd();
		// glReadBuffer(fbo_attachments[Current_Buffer]);
		// imdebugPixelsf(0, 0, screenwidth+2, screenheight+2, GL_RGBA);

		// funcgrad가 2번 이상 호출하는 경우를 위한 초기화 코드
		if (steplength==1 && PassesBeforeJFA)
		{
			steplength = (screenwidth>screenheight ? screenwidth : screenheight)/2;
			PassesBeforeJFA = false;
		}
		else if (steplength>1)
			steplength /= 2;
		else if (SecondExit)
			ExitLoop = true;
		else
		{
			steplength = pow(2.0, (additional_passes-1));
			SecondExit = true;
		}
		
		// Loop를 돌 때마다 다른 버퍼를 사용하도록 처리
		Current_Buffer = 1-Current_Buffer;
	}

	////////////////////////////////
	// Third pass, Compute energy //
	////////////////////////////////
	cgGLBindProgram(VP_Scatter);
	cgGLBindProgram(FP_Scatter);

	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, fbo_attachments[0], 
		GL_TEXTURE_RECTANGLE_NV, Site_Texture, 0);
	CheckFramebufferStatus();
	glDrawBuffer(fbo_attachments[0]);

	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	if (VP_Scatter_Size != NULL)
		cgSetParameter2f(VP_Scatter_Size, screenwidth, screenheight);

	//Bind & enable shadow map texture
	glActiveTextureARB(GL_TEXTURE0_ARB);
	// Loop 종료시 CurrentBuffer를 변경하기 때문에
	// 완료된 버퍼를 사용하기 위해 1-Current_Buffer를 선택
	glBindTexture(GL_TEXTURE_RECTANGLE_NV, Processed_Texture[1-Current_Buffer]);

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	glCallList(ScreenPointsList);
	glDisable(GL_BLEND);

	Energyf(grSite, g, f_tb_dev, screenwidth, iSiteTextureHeight, point_num, stream);

	lbfgsbcuda::CheckBuffer(g, point_num * 2, point_num * 2);

	glFinish();
	get_timestamp(end_time_func);
	elapsed_time_func = (end_time_func-start_time_func);
	total_time_func += elapsed_time_func;

	// energyf()의 결과를 반환
	f = *f_tb_host;
}

real BFGSOptimization()
{
	printf("Before init sites(point_num=%d).\n", point_num);

	// Use L-BFGS method to compute new sites
	const real epsg = EPSG;
	const real epsf = EPSF;
	const real epsx = EPSX;
	const int maxits = MAXITS;
	stpscal = 2.0f; //Set for different problems!
	int info;

	total_time = 0;
	total_time_func = 0;

	real* x;
	int* nbd;
	real* l;
	real* u;
	memAlloc<real>(&x, point_num * 2);
	memAlloc<int>(&nbd, point_num * 2);
	memAlloc<real>(&l, point_num * 2);
	memAlloc<real>(&u, point_num * 2);
	memAllocHost<real>(&f_tb_host, &f_tb_dev, 1);

	// Kernel이 처리할 수 있도록 site_list를 매핑하는 site_list_dev를 전달.
	// site_list는 InitializeSites()에서 지정
	// Device에 할당된 x에 site_list가 복사
	InitSites(x, (float*)site_list_dev, sizeof(SiteType) / sizeof(float), nbd, l, u, point_num * 2, screenwidth, screenheight);

	printf("Start optimization...");
	get_timestamp(start_time);

	int	m = 8;
	if (point_num * 2 < m)
		m = point_num * 2;

	// 내부적으로 funcgrad()를 호출
	lbfgsbminimize(point_num*2, m, x, epsg, epsf, epsx, maxits, nbd, l, u, info);
	//printf("Ending code:%d\n", info);

	get_timestamp(end_time);
	elapsed_time = (end_time-start_time);
	total_time += elapsed_time;
	printf("Done.\n JFA Time: %lf\tBFGS Time: %lf\tTotal time: %lf\t", total_time_func, elapsed_time - total_time_func, elapsed_time);
	bReCompute = false;
	
	real f = DrawVoronoi(x);

	// Device에 저장된 x가 실제 이동된 site 정보인 듯
	// 이를 Host로 복사한 후, site_list에 할당
	real* x_host = new real[point_num * 2];
	memCopy(x_host, x, point_num * 2 * sizeof(real), cudaMemcpyDeviceToHost);

	FILE* fp = fopen("Result.txt", "w");
	for(int i = 0; i < point_num; i++) {
		real ix = x_host[i * 2];
		real iy = x_host[i * 2 + 1];

		real ox = (ix + 1) * (screenwidth - 1) / 2.0 + 1.0;
		real oy = (iy + 1) * (screenheight - 1) / 2.0 + 1.0;

		if(1.0f > ox || ox > screenwidth - 1)
			continue;

		if(1.0f > oy || oy > screenheight - 1)
			continue;

		fprintf(fp, "%f, %f\n", ox, oy);
	}
	fclose(fp);

	delete[] x_host;

	memFreeHost(f_tb_host);
	memFree(x);
	memFree(nbd);
	memFree(l);
	memFree(u);

	return f;
}

real DrawVoronoi(real* xx)
{
	int i,j;

	real fEnergy = 1e20;

	GLfloat *buffer_screen = new GLfloat[screenwidth*screenheight*4];

	//////////////////////////////////////////////
	// First pass - Render the initial sites    //
	//////////////////////////////////////////////
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, FB_objects);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, fbo_attachments[0], 
		GL_TEXTURE_RECTANGLE_NV, Processed_Texture[0], 0);
	CheckFramebufferStatus();

	glClearColor(-1, -1, -1, -1);
	glClear(GL_COLOR_BUFFER_BIT);

	glDrawBuffer(fbo_attachments[0]);

	cgGLEnableProfile(VertexProfile);
	cgGLEnableProfile(FragmentProfile);

	cgGLBindProgram(VP_DrawSites);
	cgGLBindProgram(FP_DrawSites);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(1, screenwidth+1, 1, screenheight+1);
	glViewport(1, 1, screenwidth, screenheight);

	DrawSites(xx, NULL);

	// glReadBuffer(fbo_attachments[0]);
	// imdebugPixelsf(0, 0, screenwidth+2, screenheight+2, GL_RGBA);

	Current_Buffer = 1;

	/////////////////////////////////////
	// Second pass - Flood the sites   //
	/////////////////////////////////////
	cgGLBindProgram(VP_Flood);
	cgGLBindProgram(FP_Flood);

	if (VP_Flood_Size != NULL)
		cgSetParameter2f(VP_Flood_Size, screenwidth, screenheight);

	bool ExitLoop = false;
	bool SecondExit;
	int steplength;;
	SecondExit = (additional_passes==0);
	bool PassesBeforeJFA;
	PassesBeforeJFA = (additional_passes_before>0);
	if (PassesBeforeJFA)
		steplength = pow(2.0, (additional_passes_before-1));
	else
		steplength = (screenwidth>screenheight ? screenwidth : screenheight)/2;

	while (!ExitLoop)
	{
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, fbo_attachments[Current_Buffer], 
			GL_TEXTURE_RECTANGLE_NV, Processed_Texture[Current_Buffer], 0);
		CheckFramebufferStatus();
		glDrawBuffer(fbo_attachments[Current_Buffer]);

		glClearColor(-1, -1, -1, -1);
		glClear(GL_COLOR_BUFFER_BIT);

		//Bind & enable shadow map texture
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_NV, Processed_Texture[1-Current_Buffer]);
		if (VP_Flood_Steplength != NULL)
			cgSetParameter1d(VP_Flood_Steplength, steplength);

		glBegin(GL_QUADS);
			glVertex2f(1.0, 1.0);
			glVertex2f(1.0, float(screenheight+1));
			glVertex2f(float(screenwidth+1), float(screenheight+1));
			glVertex2f(float(screenwidth+1), 1.0);
		glEnd();
		glReadBuffer(fbo_attachments[Current_Buffer]);
		// imdebugPixelsf(0, 0, screenwidth+2, screenheight+2, GL_RGBA);

		if (steplength==1 && PassesBeforeJFA)
		{
			steplength = (screenwidth>screenheight ? screenwidth : screenheight)/2;
			PassesBeforeJFA = false;
		}
		else if (steplength>1)
			steplength /= 2;
		else if (SecondExit)
			ExitLoop = true;
		else
		{
			steplength = pow(2.0, (additional_passes-1));
			SecondExit = true;
		}
		Current_Buffer = 1-Current_Buffer;
	}
	glReadPixels(1,1,screenwidth,screenheight,GL_RGBA,GL_FLOAT,buffer_screen);

	///////////////////////////////
	// Test pass, Compute energy //
	///////////////////////////////
	int Current_Energy_Buffer = 0;
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, fbo_attachments[0], 
		GL_TEXTURE_RECTANGLE_NV, Energy_Texture[Current_Energy_Buffer], 0);
	CheckFramebufferStatus();
	glDrawBuffer(fbo_attachments[0]);

	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	cgGLBindProgram(VP_ComputeEnergyCentroid);
	cgGLBindProgram(FP_ComputeEnergyCentroid);

	if (FP_ComputeEnergyCentroid_Size != NULL)
		cgSetParameter2f(FP_ComputeEnergyCentroid_Size, screenwidth, screenheight);

	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB, Processed_Texture[1-Current_Buffer]);

	glBegin(GL_QUADS);
	glVertex2f(1.0, 1.0);
	glVertex2f(float(screenwidth+1), 1.0);
	glVertex2f(float(screenwidth+1), float(screenheight+1));
	glVertex2f(1.0, float(screenheight+1));
	glEnd();

	// glReadBuffer(fbo_attachments[0]);
	// imdebugPixelsf(0, 0, screenwidth+2, screenheight+2, GL_RGBA);

	Current_Energy_Buffer = 1-Current_Energy_Buffer;

	//////////////////////
	// perform reduction
	//////////////////////
	cgGLBindProgram(VP_Deduction);
	cgGLBindProgram(FP_Deduction);

	bool ExitEnergyLoop = false;
	int quad_size = int((screenwidth>screenheight?screenwidth:screenheight)/2.0+0.5);
	while (!ExitEnergyLoop)
	{
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, fbo_attachments[0], 
			GL_TEXTURE_RECTANGLE_NV, Energy_Texture[Current_Energy_Buffer], 0);
		CheckFramebufferStatus();
		glDrawBuffer(fbo_attachments[0]);

		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT);

		//Bind & enable shadow map texture
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB, Energy_Texture[1-Current_Energy_Buffer]);

		glBegin(GL_QUADS);
		glVertex2f(1.0, 1.0);
		glVertex2f(float(quad_size+1), 1.0);
		glVertex2f(float(quad_size+1), float(quad_size+1));
		glVertex2f(1.0, float(quad_size+1));
		glEnd();

		// glReadBuffer(fbo_attachments[0]);
		// imdebugPixelsf(0, 0, screenwidth+2, screenheight+2, GL_RGBA);

		if (quad_size>1)
		{
			int temp = quad_size/2;
			quad_size = temp*2==quad_size ? temp : temp+1;
		}
		else
			ExitEnergyLoop = true;
		Current_Energy_Buffer = 1-Current_Energy_Buffer;
	}
	float total_sum[4];
	// glReadBuffer(fbo_attachments[0]);
	// imdebugPixelsf(0, 0, screenwidth+2, screenheight+2, GL_RGBA);
	glReadPixels(1, 1, 1, 1, GL_RGBA, GL_FLOAT, &total_sum);
	printf("Energy: %f\n", total_sum[0]);
	fEnergy = total_sum[0];

	//////////////////////////////////////////
	// Third pass - Scatter points to sites //
	//////////////////////////////////////////
	cgGLBindProgram(VP_ScatterCentroid);
	cgGLBindProgram(FP_ScatterCentroid);

	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, fbo_attachments[0], 
		GL_TEXTURE_RECTANGLE_NV, Site_Texture, 0);
	CheckFramebufferStatus();
	glDrawBuffer(buffers[0]);

	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	if (VP_ScatterCentroid_Size != NULL)
		cgSetParameter2f(VP_ScatterCentroid_Size, screenwidth, screenheight);

	//Bind & enable shadow map texture
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_NV, Processed_Texture[1-Current_Buffer]);

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	glBegin(GL_POINTS);
	for (i=0; i<screenwidth; i++)
		for (j=0; j<screenheight; j++)
			glVertex2f(i+1.5, j+1.5);
	glEnd();
	glDisable(GL_BLEND);

	Current_Buffer = 1-Current_Buffer;

	///////////////////////////////////////
	// Fourth pass - Test stop condition //
	///////////////////////////////////////
	cgGLBindProgram(VP_DrawSitesOQ);
	cgGLBindProgram(FP_DrawSitesOQ);

	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, fbo_attachments[2], GL_RENDERBUFFER_EXT, RB_object);
	CheckFramebufferStatus();
	glDrawBuffer(fbo_attachments[2]);

	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	if (VP_DrawSitesOQ_Size != NULL)
		cgSetParameter2f(VP_DrawSitesOQ_Size, screenwidth, screenheight);

	//Bind & enable shadow map texture
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_NV, Site_Texture);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_NV, Processed_Texture[Current_Buffer]);

	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_FALSE);
	glBeginQueryARB(GL_SAMPLES_PASSED_ARB, occlusion_query);
	glBegin(GL_POINTS);
	for (i=0; i<point_num; i++)
	{
		float xx, yy;
		xx = i%screenwidth+1.5;
		yy = i/screenheight+1.5;
		glTexCoord1f(i);
		glVertex2f(xx, yy);
	}
	glEnd();
	glEndQueryARB(GL_SAMPLES_PASSED_ARB);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);

	// glReadBuffer(fbo_attachments[2]);
	// imdebugPixelsf(0, 0, screenwidth+2, screenheight+2, GL_RGBA);

	do{
		glGetQueryObjectivARB(occlusion_query, GL_QUERY_RESULT_AVAILABLE_ARB, &oq_available);
	}while(oq_available);
	glGetQueryObjectuivARB(occlusion_query, GL_QUERY_RESULT_ARB, &sampleCount);
	printf("sample count: %d\n", sampleCount);

	cgGLDisableProfile(VertexProfile);
	cgGLDisableProfile(FragmentProfile);

	////////////////////
	// compute measures
	////////////////////
	bool *bOnBoundary = new bool[point_num];
	bool *bIsHexagon = new bool[point_num];
	int *nNeighbors = new int[point_num*7];
	real *dDiameter = new real[point_num];
	real *dNeighborDist = new real[point_num];

	float site_pos[2], x, y, dist, neighbor_pos[2];
	int id, drow, dcol, nrow, ncol, neighbor_id, k;
	real dMaxDiameter, chi_id, chi;
	int nHex, nVC;

	for (id=0; id<point_num; id++)
	{
		bOnBoundary[id] = false;
		bIsHexagon[id] = true;
		nNeighbors[id*7] = 0;
		for (k=1; k<7; k++)
			nNeighbors[id*7+k] = -1;
		dDiameter[id] = -1;
		dNeighborDist[id] = 2*(screenwidth+screenheight);
	}
	dMaxDiameter = -1;
	chi = -1;
	nHex = nVC = 0;

	for (i=0; i<screenheight; i++)
	{
		for (j=0; j<screenwidth; j++)
		{
			site_pos[0] = buffer_screen[i*screenwidth*4+j*4];
			site_pos[1] = buffer_screen[i*screenwidth*4+j*4+1];
			id = int(buffer_screen[i*screenwidth*4+j*4+2]);
			x = j+1.5;
			y = i+1.5;
			site_pos[0] = (site_pos[0]-1)/screenwidth*2-1;
			site_pos[1] = (site_pos[1]-1)/screenheight*2-1;
			x = (x-1)/screenwidth*2-1;
			y = (y-1)/screenheight*2-1;
			dist = (x-site_pos[0])*(x-site_pos[0])+(y-site_pos[1])*(y-site_pos[1]);
			dist = sqrt(dist);
			dDiameter[id] = dDiameter[id]<dist ? dist : dDiameter[id];

			// traverse 9 neighbors
			for (drow=-1; drow<=1; drow++)
			{
				for (dcol=-1; dcol<=1; dcol++)
				{
					if (drow==0 && dcol==0)
						continue;
					nrow = i+drow;
					ncol = j+dcol;

					if (nrow<0 || nrow>=screenheight || ncol<0 || ncol>=screenwidth)
					{
						bOnBoundary[id] = true;
						continue;
					}

					neighbor_pos[0] = buffer_screen[nrow*screenwidth*4+ncol*4];
					neighbor_pos[1] = buffer_screen[nrow*screenwidth*4+ncol*4+1];
					neighbor_id = int(buffer_screen[nrow*screenwidth*4+ncol*4+2]);
					neighbor_pos[0] = (neighbor_pos[0]-1)/screenwidth*2-1;
					neighbor_pos[1] = (neighbor_pos[1]-1)/screenheight*2-1;
					if (neighbor_id==id)
						continue;

					dist = (neighbor_pos[0]-site_pos[0])*(neighbor_pos[0]-site_pos[0])
						   +(neighbor_pos[1]-site_pos[1])*(neighbor_pos[1]-site_pos[1]);
					dist = sqrt(dist);
					dNeighborDist[id] = dNeighborDist[id]>dist ? dist : dNeighborDist[id];

					for (k=1; k<7; k++)
					{
						if (nNeighbors[id*7+k]<0)
						{
							nNeighbors[id*7+k] = neighbor_id;
							nNeighbors[id*7]++;
							break;
						}
						else if (nNeighbors[id*7+k]==neighbor_id)
							break;
					}
					if (k==7)
						bIsHexagon[id] = false;
				}
			}
		}
	}
	for (id=0; id<point_num; id++)
	{
		if (nNeighbors[id*7]!=6)
			bIsHexagon[id] = false;
	}
	for (id=0; id<point_num; id++)
	{
		dMaxDiameter = dMaxDiameter<dDiameter[id] ? dDiameter[id] : dMaxDiameter;
		chi_id = 2*dDiameter[id]/dNeighborDist[id];
		chi = chi<chi_id ? chi_id : chi;
		if (!bOnBoundary[id])
		{
			nVC++;
		}
		if (bIsHexagon[id])
		{
			nHex++;
		}
	}

	printf("\n==== measures ====\n");
	printf("Number of VC in the middle: %d\n", nVC);
	printf("Number of hexagons: %d\n", nHex);
	printf("h: %f\n", dMaxDiameter);
	printf("chi: %f\n", chi);
	printf("==== measures ====\n\n");

	////////////////////
	// Fill Octagon & another
	////////////////////
	GLubyte *ColorTexImage = new GLubyte[screenwidth*screenheight*4];
	for (i=0; i<screenheight; i++)
	{
		for (j=0; j<screenwidth; j++)
		{
			int id = i*screenwidth+j;
			if (id<point_num)
			{
				if (bIsHexagon[id])
				{
					ColorTexImage[id*4] = 255;
					ColorTexImage[id*4+1] = 255; 
					ColorTexImage[id*4+2] = 255;
					ColorTexImage[id*4+3] = 255;
				}
				else
				{
					ColorTexImage[id*4] = 192;
					ColorTexImage[id*4+1] = 192; 
					ColorTexImage[id*4+2] = 192;
					ColorTexImage[id*4+3] = 255;
				}
			}
			else
			{
					ColorTexImage[id*4] = 
					ColorTexImage[id*4+1] = 
					ColorTexImage[id*4+2] = 
					ColorTexImage[id*4+3] = 0.0;
			}
		}
	}
	glActiveTextureARB(GL_TEXTURE2_ARB);
	glGenTextures(1, &Color_Texture);
	glBindTexture(GL_TEXTURE_RECTANGLE_NV, Color_Texture);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGBA, screenwidth,
		screenheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, ColorTexImage);

	delete ColorTexImage;

	delete [] buffer_screen;
	delete [] bOnBoundary;
	delete [] bIsHexagon;
	delete [] nNeighbors;
	delete [] dDiameter;
	delete [] dNeighborDist;

	///////////////////////////////////
	// Last pass, Display the result //
	///////////////////////////////////
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);    

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(0, screenwidth-1, 0, screenheight-1);
	glViewport(0, 0, screenwidth, screenheight);

	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_NV, Processed_Texture[Current_Buffer]);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_NV, Site_Texture);

	cgGLEnableProfile(VertexProfile);
	cgGLEnableProfile(FragmentProfile);

	cgGLBindProgram(VP_FinalRender);
	cgGLBindProgram(FP_FinalRender);

	if (FP_FinalRender_Size != NULL)
		cgSetParameter2f(FP_FinalRender_Size, screenwidth, screenheight);

	// Set parameters of fragment program

	glBegin(GL_QUADS);
		glVertex2f(0.0, 0.0);
		glVertex2f(0.0, float(screenheight));
		glVertex2f(float(screenwidth), float(screenheight));
		glVertex2f(float(screenwidth), 0.0);
	glEnd();

	cgGLDisableProfile(VertexProfile);
	cgGLDisableProfile(FragmentProfile);

	DrawSites(xx, NULL);

	return fEnergy;
}

void Display(void)
{
	static real fEnergy = 1e20;
	static real fEnergyBase = 1e20;
	bool isFirst = true;
	real t0 = 0;
	real tk = 0;

	real k = 0;
	real K = 1;

	if(bReCompute)
		printf("Recompute\n");
	else
		printf("Do not compute\n");

	int i = 0;
	while (bReCompute && k < K)
	{
		real fStar = BFGSOptimization();
		real df = fStar - fEnergy;
		if(df < 0) {
			//X <- X*
			CopySite(site_list_x, site_list, point_num);
			fEnergy = fStar;
			printf("Lower! e = %lf\n", fEnergy);
		} else {
			if(isFirst) {
				//initialize T0
				t0 = df * 4.48142;
				isFirst = false;
			}
			tk = t0 * pow(1.0 - k / K, 6);
			real acc = exp(-df / tk);
			real r = (float)rand() / (float)(RAND_MAX);
			if(r < acc) {
				//X <- X*
				CopySite(site_list_x, site_list, point_num);
				fEnergy = fStar;
				printf("Accepted! e = %lf, acc = %lf, tk = %lf\n", fEnergy, acc, tk);
			} else {
				printf("Rejected! e* = %lf > e = %lf\n", fStar, fEnergy);
			}
		}
		if(fStar < fEnergyBase) {
			//XBase <- X*
			CopySite(site_list_x_bar, site_list, point_num);
			fEnergyBase = fStar;
			printf("Base Updated!\n");
		}

		//Perturb X -> X*
		k = k + 0.1;
		for(int i = 0; i < point_num * 2; i++) {
			if(i % 2 == 0) {
				site_list_x[i] = site_list_x[i] + 
					((real)rand() / (real)RAND_MAX * 2.0 - 1.0) * 
					site_perturb_step * (real)screenwidth;
				site_list_x[i] = __max(1, __min(screenwidth - 1, site_list_x[i]));
			}
			else {
				site_list_x[i] = site_list_x[i] + 
					((real)rand() / (real)RAND_MAX * 2.0 - 1.0) * 
					site_perturb_step * (real)screenwidth;
				site_list_x[i] = __max(1, __min(screenwidth - 1, site_list_x[i]));
			}
		}
		CopySite(site_list, site_list_x, point_num);
		printf("* Energy Base = %lf *\n", fEnergyBase);
	}

	if(bReCompute) {
		CopySite(site_list, site_list_x_bar, point_num);
		real fStar = BFGSOptimization();
	}

	glFinish();

	glutSwapBuffers();
}

void Keyboard(unsigned char key, int x, int y)
{
	int i;

	switch (key)
	{
	case '0':
		{
			DestroySites();
			InitializeSites(point_num);

			glutPostRedisplay();
			break;
		}
	case '.':
		{
			point_num+=100;
			point_num = point_num;

			DestroySites();
			InitializeSites(point_num);

			glutPostRedisplay();
			break;
		}
	case ',':
		{
			bool decreased = false;
			if (point_num>0)
			{
				point_num-=100;
				decreased = true;
			}

			if (decreased)
			{
				point_num = point_num;

				DestroySites();
				InitializeSites(point_num);

				glutPostRedisplay();
			}
			break;
		}
	case 'q':
		{
			exit(0);
			break;
		}
	}
}

void InitializeGlut(int *argc, char *argv[])
{
	int i,j;

	glutInit(argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(screenwidth, screenheight);
	glutCreateWindow(argv[0]);
	glutDisplayFunc(Display);
	glutKeyboardFunc(Keyboard);

	// Support mapped pinned allocations
	cudaSetDeviceFlags(cudaDeviceMapHost);

	cudaGLSetGLDevice(0);

	cublasCreate_v2(&cublasHd);

	glewInit();
	GLint max_texture_size;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
	if(max_texture_size < screenwidth || screenwidth < screenheight) {
		printf("Max size of texttur(%d) is less than screensize(%d, %d)\n", max_texture_size, screenwidth, screenheight);
		exit(0);
	}

	//Create the textures
	glActiveTextureARB(GL_TEXTURE0_ARB);

	// 처리용 텍스쳐 2장
	// Q. 왜 2장일까?
	glGenTextures(2, Processed_Texture);
	glBindTexture(GL_TEXTURE_RECTANGLE_NV, Processed_Texture[0]);
	glTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGBA32F_ARB, screenwidth+2, screenheight+2, 0, 
		GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_WRAP_T, GL_CLAMP);

	glBindTexture(GL_TEXTURE_RECTANGLE_NV, Processed_Texture[1]);
	glTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGBA32F_ARB, screenwidth+2, screenheight+2, 0, 
		GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_WRAP_T, GL_CLAMP);

	// Site용 텍스쳐
	// Q. 처리용과 별개인 이유는?
	glGenTextures(1, &Site_Texture);
	glBindTexture(GL_TEXTURE_RECTANGLE_NV, Site_Texture);
	glTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGBA32F_ARB, screenwidth+2, screenheight+2, 0, 
		GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_WRAP_T, GL_CLAMP);

	// Registers the texture or renderbuffer object specified by image for access by CUDA.
	// A handle to the registered object is returned as resource
	cutilSafeCall(cudaGraphicsGLRegisterImage(&grSite, Site_Texture, 
		GL_TEXTURE_RECTANGLE_NV, cudaGraphicsMapFlagsReadOnly));

	// 에너지용 텍스쳐
	// 처리용과 동일한 2장
	// Q. 왜??
	glGenTextures(2, Energy_Texture);
	glBindTexture(GL_TEXTURE_RECTANGLE_NV, Energy_Texture[0]);
	glTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGBA32F_ARB, screenwidth+2, screenheight+2, 0, 
		GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_WRAP_T, GL_CLAMP);

	glBindTexture(GL_TEXTURE_RECTANGLE_NV, Energy_Texture[1]);
	glTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGBA32F_ARB, screenwidth+2, screenheight+2, 0, 
		GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_WRAP_T, GL_CLAMP);

	// 인덱스용 텍스쳐
	// 인덱스를 컬러로 표현
	glGenTextures(1, &IndexColor_Texture);
	glBindTexture(GL_TEXTURE_RECTANGLE_NV, IndexColor_Texture);
	glTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGBA, screenwidth, screenheight, 0, 
		GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_WRAP_T, GL_CLAMP);

	// Render Buffer Object
	glGenFramebuffersEXT(1, &RB_object);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, RB_object);
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA32F_ARB, screenwidth+2, screenheight+2);

	// Frame(?) Buffer Object
	glGenFramebuffersEXT(1, &FB_objects);

	// ????
	// NVIDIA 확인이라는 점만 확인
	// http://developer.download.nvidia.com/opengl/specs/nvOpenGLspecs.pdf‎
	glGetQueryiv(GL_SAMPLES_PASSED_ARB, GL_QUERY_COUNTER_BITS_ARB, &oq_bitsSupported);
	glGenQueriesARB(1, &occlusion_query);

	InitCg();

	// 미리 컴파일된 화면 픽셀 목록
	ScreenPointsList = glGenLists(1);
	glNewList(ScreenPointsList, GL_COMPILE);
	glBegin(GL_POINTS);
	for (i=0; i<screenwidth; i++)
		for (j=0; j<screenheight; j++)
			glVertex2f(i+1.5, j+1.5);
	glEnd();
	glEndList();

}

void DestroyGlut()
{
	cudaGraphicsUnregisterResource(grSite);
	cudaGraphicsUnregisterResource(grVbo);

	cublasDestroy_v2(cublasHd);

	cudaDeviceReset();
}

void CgErrorCallback(void)
{
	CGerror lastError = cgGetError();
	if (lastError)
	{
		printf("%s\n", cgGetErrorString(lastError));
		printf("%s\n", cgGetLastListing(Context));
		printf("Cg error, exiting...\n");
		exit(0);
	}
}

void InitializeSites(int point_num)
{
	int i, j, index;
	int v_per_site;
	VertexSiteType v;

	additional_passes = 0;
	additional_passes_before = 0;

	iSiteTextureHeight = point_num/screenwidth+1;

	bReCompute = true;

	// Allicate Host-Mem(site_list). for reading from Device(site_list_dev).
	memAllocHost<SiteType>(&site_list, &site_list_dev, point_num);

	// Buffer for swap site_list
	site_list_x = new float[(point_num) * 2];
	site_list_x_bar = new float[(point_num) * 2];
	
	site_perturb_step = 0.5f / sqrtf(point_num);

	if(!bReadSitesFromFile) {
		// ------------------------------------------------------------
		// Randomize Site-position
		// ------------------------------------------------------------
		bool *FlagArray = new bool[screenwidth*screenheight];
		for (i=0; i<screenwidth*screenheight; i++)
			FlagArray[i] = false;

		for (i=0; i<point_num; i++)
		{
			SiteType s;

			v.x = (double)rand()/(double)RAND_MAX*(screenwidth-1.0)+1.0;
			v.y = (double)rand()/(double)RAND_MAX*(screenheight-1.0)+1.0;
			while(true) {
				index = int(v.y)*screenwidth+int(v.x);

				if (FlagArray[index])
				{
					printf("\nDuplicate site found: #%d\n", i);

					v.x = v.x + ((float)rand() / (float)RAND_MAX * 2.0f - 1.0f) * (float)(screenwidth-1);
					v.y = v.y + ((float)rand() / (float)RAND_MAX * 2.0f - 1.0f) * (float)(screenwidth-1);

					while(v.x > (float)(screenwidth - 1)) {
						v.x -= (float)screenwidth;
					}

					while(v.x < 1.0f) {
						v.x += (float)screenwidth;
					}

					while(v.y > (float)(screenheight - 1)) {
						v.y -= (float)screenheight;
					}

					while(v.y < 1.0f) {
						v.y += (float)screenheight;
					}
				}
				else
				{
					FlagArray[index] = true;
					break;
				}
			}

			s.vertices[0] = v;
			s.color[0] = (float)rand()/(float)RAND_MAX;
			s.color[1] = (float)rand()/(float)RAND_MAX;
			s.color[2] = (float)rand()/(float)RAND_MAX;
			s.color[3] = i;
			site_list[i] = s;
		}

		delete FlagArray;
	}
	else
	{
		// ------------------------------------------------------------
		// Read Site-position from input file.
		// ------------------------------------------------------------
		FILE* fp = fopen("init.txt", "r");


		for (i=0; i<point_num; i++)
		{
			SiteType s;

			fscanf(fp, "%f, %f\n", &v.x, &v.y);

			s.vertices[0] = v;
			s.color[0] = (float)rand()/(float)RAND_MAX;
			s.color[1] = (float)rand()/(float)RAND_MAX;
			s.color[2] = (float)rand()/(float)RAND_MAX;
			s.color[3] = i;
			site_list[i] = s;
		}

		fclose(fp);
	}

	// ------------------------------------------------------------
	// Set Color_Texture as Site-Index
	// ------------------------------------------------------------
	GLubyte *ColorTexImage = new GLubyte[screenwidth*screenheight*4];
	for (i=0; i<screenheight; i++)
		for (j=0; j<screenwidth; j++)
		{
			int id = i*screenwidth+j;
			if (id<point_num)
			{
				ColorTexImage[id*4] = site_list[id].color[0]*255;
				ColorTexImage[id*4+1] = site_list[id].color[1]*255; 
				ColorTexImage[id*4+2] = site_list[id].color[2]*255;
				ColorTexImage[id*4+3] = 255;
			}
			else
			{
				ColorTexImage[id*4] = 
				ColorTexImage[id*4+1] = 
				ColorTexImage[id*4+2] = 
				ColorTexImage[id*4+3] = 0.0;
			}
		}

	glActiveTextureARB(GL_TEXTURE2_ARB);
	glGenTextures(1, &Color_Texture);
	glBindTexture(GL_TEXTURE_RECTANGLE_NV, Color_Texture);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGBA, screenwidth,
		screenheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, ColorTexImage);

	delete ColorTexImage;

	// ------------------------------------------------------------
	// Create Vertext-Buffer-Oobject(VBO) & Register graphic resource for VBO
	// DrawSites()에서 사용. CUDA를 통해 x를 VBO에 저장하기 위해 grVBO가 필요
	// ------------------------------------------------------------
	glGenBuffersARB(1, &vboId);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, vboId);
	glBufferDataARB(GL_ARRAY_BUFFER_ARB, point_num * sizeof(VertexSiteType), NULL, GL_DYNAMIC_DRAW_ARB);
	cudaGraphicsGLRegisterBuffer(&grVbo, vboId, cudaGraphicsMapFlagsWriteDiscard);

	// ------------------------------------------------------------
	// Create Color-Buffer-Object(CBO) and set from site_list
	// ------------------------------------------------------------
	glGenBuffersARB(1, &colorboId);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, colorboId);
	glBufferDataARB(GL_ARRAY_BUFFER_ARB, point_num * sizeof(float) * 4, NULL, GL_DYNAMIC_DRAW_ARB);

	GLvoid* pointer = glMapBufferARB(GL_ARRAY_BUFFER_ARB, GL_WRITE_ONLY_ARB);
	float* sitelist = (float*)pointer;
	for (i=0; i<point_num; i++)
	{
		sitelist[i * 4 + 0] = site_list[i].color[0];
		sitelist[i * 4 + 1] = site_list[i].color[1];
		sitelist[i * 4 + 2] = site_list[i].color[2];
		sitelist[i * 4 + 3] = site_list[i].color[3];
	}
	glUnmapBufferARB(GL_ARRAY_BUFFER_ARB);
}

void DestroySites()
{
	glDeleteBuffersARB(1, &vboId);
	glDeleteBuffersARB(1, &colorboId);

	delete[] site_list_x;
	delete[] site_list_x_bar;
	cudaFreeHost(site_list);
}

void InitCg()
{
	cgSetErrorCallback(CgErrorCallback);
	Context = cgCreateContext();
	VertexProfile = cgGLGetLatestProfile(CG_GL_VERTEX);
	printf("VertexProfile %s\n", cgGetProfileString(VertexProfile));
	cgGLSetOptimalOptions(VertexProfile);
	FragmentProfile = cgGLGetLatestProfile(CG_GL_FRAGMENT);
	printf("FragmentProfile %s\n", cgGetProfileString(FragmentProfile));
	cgGLSetOptimalOptions(FragmentProfile);

	VP_DrawSites = cgCreateProgramFromFile(Context,
							CG_SOURCE, "VP_DrawSites.cg",
							VertexProfile,
							NULL, NULL);
	FP_DrawSites = cgCreateProgramFromFile(Context,
							CG_SOURCE, "FP_DrawSites.cg",
							FragmentProfile,
							NULL, NULL);
	VP_Flood = cgCreateProgramFromFile(Context,
							CG_SOURCE, "VP_Flood.cg",
							VertexProfile,
							NULL, NULL);
	FP_Flood = cgCreateProgramFromFile(Context,
							CG_SOURCE, "FP_Flood.cg",
							FragmentProfile,
							NULL, NULL);
	VP_Scatter = cgCreateProgramFromFile(Context,
							CG_SOURCE, "VP_Scatter.cg",
							VertexProfile,
							NULL, NULL);
	FP_Scatter = cgCreateProgramFromFile(Context,
							CG_SOURCE, "FP_Scatter.cg",
							FragmentProfile,
							NULL, NULL);
	VP_DrawNewSites = cgCreateProgramFromFile(Context,
							CG_SOURCE, "VP_DrawNewSites.cg",
							VertexProfile,
							NULL, NULL);
	FP_DrawNewSites = cgCreateProgramFromFile(Context,
							CG_SOURCE, "FP_DrawNewSites.cg",
							FragmentProfile,
							NULL, NULL);
	VP_DrawSitesOQ = cgCreateProgramFromFile(Context,
							CG_SOURCE, "VP_DrawSitesOQ.cg",
							VertexProfile,
							NULL, NULL);
	FP_DrawSitesOQ = cgCreateProgramFromFile(Context,
							CG_SOURCE, "FP_DrawSitesOQ.cg",
							FragmentProfile,
							NULL, NULL);
	VP_FinalRender = cgCreateProgramFromFile(Context,
							CG_SOURCE, "VP_FinalRender.cg",
							VertexProfile,
							NULL, NULL);
	FP_FinalRender = cgCreateProgramFromFile(Context,
							CG_SOURCE, "FP_FinalRender.cg",
							FragmentProfile,
							NULL, NULL);
	VP_ComputeEnergy = cgCreateProgramFromFile(Context,
							CG_SOURCE, "VP_ComputeEnergy.cg",
							VertexProfile,
							NULL, NULL);
	FP_ComputeEnergy = cgCreateProgramFromFile(Context,
							CG_SOURCE, "FP_ComputeEnergy.cg",
							FragmentProfile,
							NULL, NULL);
	VP_Deduction = cgCreateProgramFromFile(Context,
							CG_SOURCE, "VP_Deduction.cg",
							VertexProfile,
							NULL, NULL);
	FP_Deduction = cgCreateProgramFromFile(Context,
							CG_SOURCE, "FP_Deduction.cg",
							FragmentProfile,
							NULL, NULL);
	VP_ComputeEnergyCentroid = cgCreateProgramFromFile(Context,
							CG_SOURCE, "VP_ComputeEnergyCentroid.cg",
							VertexProfile,
							NULL, NULL);
	FP_ComputeEnergyCentroid = cgCreateProgramFromFile(Context,
							CG_SOURCE, "FP_ComputeEnergyCentroid.cg",
							FragmentProfile,
							NULL, NULL);
	VP_ScatterCentroid = cgCreateProgramFromFile(Context,
							CG_SOURCE, "VP_ScatterCentroid.cg",
							VertexProfile,
							NULL, NULL);
	FP_ScatterCentroid = cgCreateProgramFromFile(Context,
							CG_SOURCE, "FP_ScatterCentroid.cg",
							FragmentProfile,
							NULL, NULL);

	if(VP_DrawSites != NULL)
	{
		cgGLLoadProgram(VP_DrawSites);
	}
	if(FP_DrawSites != NULL)
	{
		cgGLLoadProgram(FP_DrawSites);
	}

	if(VP_Flood != NULL)
	{
		cgGLLoadProgram(VP_Flood);

		// Bind parameters to give access to variables in the shader
		VP_Flood_Steplength = cgGetNamedParameter(VP_Flood, "steplength");
		VP_Flood_Size = cgGetNamedParameter(VP_Flood, "size");
	}
	if(FP_Flood != NULL)
	{
		cgGLLoadProgram(FP_Flood);
	}

	if(VP_Scatter != NULL)
	{
		cgGLLoadProgram(VP_Scatter);

		// Bind parameters to give access to variables in the shader
		VP_Scatter_Size = cgGetNamedParameter(VP_Scatter, "size");
	}
	if(FP_Scatter != NULL)
	{
		cgGLLoadProgram(FP_Scatter);
	}

	if(VP_DrawNewSites != NULL)
	{
		cgGLLoadProgram(VP_DrawNewSites);

		// Bind parameters to give access to variables in the shader
		VP_DrawNewSites_Size = cgGetNamedParameter(VP_DrawNewSites, "size");
	}
	if(FP_DrawNewSites != NULL)
	{
		cgGLLoadProgram(FP_DrawNewSites);
	}

	if(VP_DrawSitesOQ != NULL)
	{
		cgGLLoadProgram(VP_DrawSitesOQ);

		// Bind parameters to give access to variables in the shader
		VP_DrawSitesOQ_Size = cgGetNamedParameter(VP_DrawSitesOQ, "size");
	}
	if(FP_DrawSitesOQ != NULL)
	{
		cgGLLoadProgram(FP_DrawSitesOQ);
	}

	if(VP_FinalRender != NULL)
	{
		cgGLLoadProgram(VP_FinalRender);
	}
	if(FP_FinalRender != NULL)
	{
		cgGLLoadProgram(FP_FinalRender);

		// Bind parameters to give access to variables in the shader
		FP_FinalRender_Size = cgGetNamedParameter(FP_FinalRender, "size");
	}

	if(VP_ComputeEnergy != NULL)
	{
		cgGLLoadProgram(VP_ComputeEnergy);
	}
	if(FP_ComputeEnergy != NULL)
	{
		cgGLLoadProgram(FP_ComputeEnergy);

		// Bind parameters to give access to variables in the shader
		FP_ComputeEnergy_Size = cgGetNamedParameter(FP_ComputeEnergy, "size");
	}

	if(VP_Deduction != NULL)
	{
		cgGLLoadProgram(VP_Deduction);
	}
	if(FP_Deduction != NULL)
	{
		cgGLLoadProgram(FP_Deduction);
	}

	if(VP_ComputeEnergyCentroid != NULL)
	{
		cgGLLoadProgram(VP_ComputeEnergyCentroid);
	}
	if(FP_ComputeEnergyCentroid != NULL)
	{
		cgGLLoadProgram(FP_ComputeEnergyCentroid);

		// Bind parameters to give access to variables in the shader
		FP_ComputeEnergyCentroid_Size = cgGetNamedParameter(FP_ComputeEnergyCentroid, "size");
	}

	if(VP_ScatterCentroid != NULL)
	{
		cgGLLoadProgram(VP_ScatterCentroid);

		// Bind parameters to give access to variables in the shader
		VP_ScatterCentroid_Size = cgGetNamedParameter(VP_ScatterCentroid, "size");
	}
	if(FP_ScatterCentroid != NULL)
	{
		cgGLLoadProgram(FP_ScatterCentroid);
	}
}

void DestroyCg()
{
	cgDestroyProgram(VP_DrawSites);
	cgDestroyProgram(FP_DrawSites);
	cgDestroyProgram(VP_Flood);
	cgDestroyProgram(FP_Flood);
	cgDestroyProgram(VP_FinalRender);
	cgDestroyProgram(FP_FinalRender);
	cgDestroyContext(Context);
}

int main(int argc, char *argv[])
{
	point_num = 8000;
	screenwidth = screenheight = 1024;

	if (argc != 4) {
		printf("Usage : %s numOfSites screenWidth readSitesFromFile\n");
		exit(-1);
	}

	point_num = atoi(argv[1]);
	screenwidth = atoi(argv[2]);
	bReadSitesFromFile = atoi(argv[3]);

	if(screenwidth <= 1 || point_num < 2) {
		printf("Invalid Args!\n");
		return -1;
	}

	screenheight = screenwidth; // * 5 / 8;

	InitializeGlut(&argc, argv);

	srand((unsigned)time(NULL));

	InitializeSites(point_num);

	glutMainLoop();
	
	DestroySites();

	DestroyCg();

	DestroyGlut();

	return 0;
}
