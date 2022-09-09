#include "GSH_Deko3d.h"
#include "../../gs/GsPixelFormats.h"

#include <climits>
#include <cstring>

#include <switch.h>
#include <deko3d.h>

#define ALIGN(x, align) (((x) + (align) - 1) & ~((align) - 1))

#define PS2_FRAMEBUFFER_HEIGHT 1024

extern "C" char _binary_triangle_vsh_dksh_start, _binary_triangle_vsh_dksh_end;
extern "C" char _binary_color_fsh_dksh_start, _binary_color_fsh_dksh_end;

struct Vertex {
	float x, y, z;
	uint32_t color;
};

// Define the desired number of framebuffers
#define FB_NUM 2

// Define the desired framebuffer resolution (here we set it to 720p).
#define FB_WIDTH  1280
#define FB_HEIGHT 720

// Remove above and uncomment below for 1080p
//#define FB_WIDTH  1920
//#define FB_HEIGHT 1080

// Define the size of the memory block that will hold code
#define CODEMEMSIZE (64*1024)

// Define the size of the memory block that will hold command lists
#define CMDMEMSIZE (16*1024)

#define VERTEX_BUFFER_ENTRIES	USHRT_MAX

static DkDevice g_device;
static DkMemBlock g_framebufferMemBlock;
static DkImage g_framebuffers[FB_NUM];
static DkSwapchain g_swapchain;

static DkMemBlock g_depthMemBlock;
static DkImage g_depthbuffer;

static DkMemBlock g_codeMemBlock;
static uint32_t g_codeMemOffset;
static DkShader g_vertexShader;
static DkShader g_fragmentShader;

static DkMemBlock g_bindFbCmdbufMemBlock;
static DkCmdBuf g_bindFbCmdbuf;
static DkCmdList g_cmdsBindFramebuffer[FB_NUM];

static DkMemBlock g_cmdbufMemBlock;
static DkCmdBuf g_cmdbuf;
static DkCmdList g_cmdsRender;

static DkMemBlock g_vtxBufferMemBlock;
static uint32_t g_vtxBufferHead;

static DkMemBlock g_unifMemBlock;

static DkQueue g_renderQueue;

static inline uint32 MakeColor(uint8 r, uint8 g, uint8 b, uint8 a)
{
	return (a << 24) | (b << 16) | (g << 8) | (r);
}

static void dk_debug_callback(void *userData, const char *context, DkResult result, const char *message)
{
	char description[256];

	if (result == DkResult_Success) {
		printf("deko3d debug callback: context: %s, message: %s, result %d",
		    context, message, result);
	} else {
		snprintf(description, sizeof(description), "context: %s, message: %s, result %d",
		         context, message, result);
		printf("deko3d fatal error: %s\n", description);
	}
}

static void loadShaderMemory(DkShader* pShader, const void *addr, size_t size)
{
	uint32_t codeOffset = g_codeMemOffset;
	g_codeMemOffset += ALIGN(size, DK_SHADER_CODE_ALIGNMENT);

	memcpy((uint8_t*)dkMemBlockGetCpuAddr(g_codeMemBlock) + codeOffset, addr, size);

	DkShaderMaker shaderMaker;
	dkShaderMakerDefaults(&shaderMaker, g_codeMemBlock, codeOffset);
	dkShaderInitialize(pShader, &shaderMaker);
}

void CGSH_Deko3d::InitializeImpl()
{
	// Create the device, which is the root object
	DkDeviceMaker deviceMaker;
	dkDeviceMakerDefaults(&deviceMaker);
	deviceMaker.userData = NULL;
	deviceMaker.cbDebug = dk_debug_callback;
	g_device = dkDeviceCreate(&deviceMaker);

	// Calculate layout for the framebuffers
	DkImageLayoutMaker imageLayoutMaker;
	dkImageLayoutMakerDefaults(&imageLayoutMaker, g_device);
	imageLayoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression;
	imageLayoutMaker.format = DkImageFormat_RGBA8_Unorm;
	imageLayoutMaker.dimensions[0] = FB_WIDTH;
	imageLayoutMaker.dimensions[1] = FB_HEIGHT;

	// Calculate layout for the framebuffers
	DkImageLayout framebufferLayout;
	dkImageLayoutInitialize(&framebufferLayout, &imageLayoutMaker);

	// Retrieve necessary size and alignment for the framebuffers
	uint32_t framebufferSize  = dkImageLayoutGetSize(&framebufferLayout);
	uint32_t framebufferAlign = dkImageLayoutGetAlignment(&framebufferLayout);
	framebufferSize = ALIGN(framebufferSize, framebufferAlign);

	// Create a memory block that will host the framebuffers
	DkMemBlockMaker memBlockMaker;
	dkMemBlockMakerDefaults(&memBlockMaker, g_device, FB_NUM*framebufferSize);
	memBlockMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
	g_framebufferMemBlock = dkMemBlockCreate(&memBlockMaker);

	// Initialize the framebuffers with the layout and backing memory we've just created
	DkImage const* swapchainImages[FB_NUM];
	for (unsigned i = 0; i < FB_NUM; i++) {
		swapchainImages[i] = &g_framebuffers[i];
		dkImageInitialize(&g_framebuffers[i], &framebufferLayout, g_framebufferMemBlock, i*framebufferSize);
	}

	/* Depth buffer */
	dkImageLayoutMakerDefaults(&imageLayoutMaker, g_device);
	imageLayoutMaker.flags = DkImageFlags_UsageRender | DkImageFlags_HwCompression;
	imageLayoutMaker.format = DkImageFormat_Z24S8;
	imageLayoutMaker.dimensions[0] = FB_WIDTH;
	imageLayoutMaker.dimensions[1] = FB_HEIGHT;	

	DkImageLayout depthLayout;
	dkImageLayoutInitialize(&depthLayout, &imageLayoutMaker);

	uint32_t depthSize  = dkImageLayoutGetSize(&depthLayout);
	uint32_t depthAlign = dkImageLayoutGetAlignment(&depthLayout);
	depthSize = ALIGN(depthSize, depthAlign);

	dkMemBlockMakerDefaults(&memBlockMaker, g_device, depthSize);
	memBlockMaker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
	g_depthMemBlock = dkMemBlockCreate(&memBlockMaker);

	dkImageInitialize(&g_depthbuffer, &depthLayout, g_depthMemBlock, 0);

	// Create a swapchain out of the framebuffers we've just initialized
	DkSwapchainMaker swapchainMaker;
	dkSwapchainMakerDefaults(&swapchainMaker, g_device, nwindowGetDefault(), swapchainImages, FB_NUM);
	g_swapchain = dkSwapchainCreate(&swapchainMaker);

	// Create a memory block onto which we will load shader code
	dkMemBlockMakerDefaults(&memBlockMaker, g_device, CODEMEMSIZE);
	memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code;
	g_codeMemBlock = dkMemBlockCreate(&memBlockMaker);
	g_codeMemOffset = 0;

	// Load our shaders (both vertex and fragment)
	loadShaderMemory(&g_vertexShader, &_binary_triangle_vsh_dksh_start, (uintptr_t)&_binary_triangle_vsh_dksh_end - (uintptr_t)&_binary_triangle_vsh_dksh_start);
	loadShaderMemory(&g_fragmentShader, &_binary_color_fsh_dksh_start, (uintptr_t)&_binary_color_fsh_dksh_end - (uintptr_t)&_binary_color_fsh_dksh_start);

	// Create a memory block which will be used for recording command lists using a command buffer
	dkMemBlockMakerDefaults(&memBlockMaker, g_device, CMDMEMSIZE);
	memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
	g_cmdbufMemBlock = dkMemBlockCreate(&memBlockMaker);

	// Create a command buffer object
	DkCmdBufMaker cmdbufMaker;
	dkCmdBufMakerDefaults(&cmdbufMaker, g_device);
	g_cmdbuf = dkCmdBufCreate(&cmdbufMaker);

	// Feed our memory to the command buffer so that we can start recording commands
	dkCmdBufAddMemory(g_cmdbuf, g_cmdbufMemBlock, 0, dkMemBlockGetSize(g_cmdbufMemBlock));

	// Create a memory block which will be used for recording command lists using a command buffer
	dkMemBlockMakerDefaults(&memBlockMaker, g_device, 4096);
	memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
	g_bindFbCmdbufMemBlock = dkMemBlockCreate(&memBlockMaker);

	// Create a command buffer object
	dkCmdBufMakerDefaults(&cmdbufMaker, g_device);
	g_bindFbCmdbuf = dkCmdBufCreate(&cmdbufMaker);

	// Feed our memory to the command buffer so that we can start recording commands
	dkCmdBufAddMemory(g_bindFbCmdbuf, g_bindFbCmdbufMemBlock, 0, dkMemBlockGetSize(g_bindFbCmdbufMemBlock));

	// Generate a command list for each framebuffer, which will bind each of them as a render target
	DkImageView depthView;
	dkImageViewDefaults(&depthView, &g_depthbuffer);
	for (unsigned i = 0; i < FB_NUM; i++) {
		DkImageView imageView;
		dkImageViewDefaults(&imageView, &g_framebuffers[i]);
		dkCmdBufBindRenderTarget(g_bindFbCmdbuf, &imageView, &depthView);
		g_cmdsBindFramebuffer[i] = dkCmdBufFinishList(g_bindFbCmdbuf);
	}

	// Allocate vertex buffer
	dkMemBlockMakerDefaults(&memBlockMaker, g_device, ALIGN(sizeof(Vertex) * VERTEX_BUFFER_ENTRIES, 4096));
	memBlockMaker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
	g_vtxBufferMemBlock = dkMemBlockCreate(&memBlockMaker);
	g_vtxBufferHead = 0;

	// Allocate uniform buffer
	dkMemBlockMakerDefaults(&memBlockMaker, g_device, ALIGN(sizeof(float) * 4 * 4, 4096));
	memBlockMaker.flags = DkMemBlockFlags_GpuCached;
	g_unifMemBlock = dkMemBlockCreate(&memBlockMaker);

	// Create a queue, to which we will submit our command lists
	DkQueueMaker queueMaker;
	dkQueueMakerDefaults(&queueMaker, g_device);
	queueMaker.flags = DkQueueFlags_Graphics;
	g_renderQueue = dkQueueCreate(&queueMaker);

	BeginFrame();
}

void CGSH_Deko3d::ReleaseImpl()
{
	EndFrame();
	
	// Make sure the rendering queue is idle before destroying anything
	dkQueueWaitIdle(g_renderQueue);

	// Destroy all the resources we've created
	dkQueueDestroy(g_renderQueue);
	dkCmdBufDestroy(g_cmdbuf);
	dkCmdBufDestroy(g_bindFbCmdbuf);
	dkMemBlockDestroy(g_cmdbufMemBlock);
	dkMemBlockDestroy(g_bindFbCmdbufMemBlock);
	dkMemBlockDestroy(g_codeMemBlock);
	dkSwapchainDestroy(g_swapchain);
	dkMemBlockDestroy(g_framebufferMemBlock);
	dkMemBlockDestroy(g_depthMemBlock);
	dkMemBlockDestroy(g_vtxBufferMemBlock);
	dkMemBlockDestroy(g_unifMemBlock);
	dkDeviceDestroy(g_device);
}

void CGSH_Deko3d::BeginFrame()
{
	// Declare structs that will be used for binding state
	DkViewport viewport = { 0.0f, 0.0f, (float)FB_WIDTH, (float)FB_HEIGHT, 0.0f, 1.0f };
	DkScissor scissor = { 0, 0, FB_WIDTH, FB_HEIGHT };
	DkShader const* shaders[] = { &g_vertexShader, &g_fragmentShader };
	DkRasterizerState rasterizerState;
	DkColorState colorState;
	DkColorWriteState colorWriteState;
	DkDepthStencilState depthStencilState;

	constexpr DkVtxAttribState VertexAttribState[2] =
	{
		DkVtxAttribState{ 0, 0, 0, DkVtxAttribSize_3x32, DkVtxAttribType_Float, 0 },
		DkVtxAttribState{ 0, 0, offsetof(Vertex, color), DkVtxAttribSize_4x8, DkVtxAttribType_Unorm, 0 },
	};

	constexpr DkVtxBufferState VertexBufferState[1] =
	{
		DkVtxBufferState{ sizeof(Vertex), 0 },
	};

	// Initialize state structs
	dkRasterizerStateDefaults(&rasterizerState);
	rasterizerState.cullMode = DkFace_None;
	rasterizerState.frontFace = DkFrontFace_CW;
	dkColorStateDefaults(&colorState);
	dkColorWriteStateDefaults(&colorWriteState);
	dkDepthStencilStateDefaults(&depthStencilState);

	dkCmdBufClear(g_cmdbuf);

	/* Start recording the command list */
	dkCmdBufSetViewports(g_cmdbuf, 0, &viewport, 1);
	dkCmdBufSetScissors(g_cmdbuf, 0, &scissor, 1);
	dkCmdBufClearColorFloat(g_cmdbuf, 0, DkColorMask_RGBA, 0.125f, 0.294f, 0.478f, 1.0f);
	dkCmdBufClearDepthStencil(g_cmdbuf, true, 0.0f, 0xFF, 0);
	dkCmdBufBindShaders(g_cmdbuf, DkStageFlag_GraphicsMask, shaders, sizeof(shaders)/sizeof(shaders[0]));
	dkCmdBufBindUniformBuffer(g_cmdbuf, DkStage_Vertex, 0, dkMemBlockGetGpuAddr(g_unifMemBlock), dkMemBlockGetSize(g_unifMemBlock));
	dkCmdBufBindRasterizerState(g_cmdbuf, &rasterizerState);
	dkCmdBufBindColorState(g_cmdbuf, &colorState);
	dkCmdBufBindColorWriteState(g_cmdbuf, &colorWriteState);
	dkCmdBufBindDepthStencilState(g_cmdbuf, &depthStencilState);
	dkCmdBufBindVtxAttribState(g_cmdbuf, VertexAttribState, 2);
	dkCmdBufBindVtxBufferState(g_cmdbuf, VertexBufferState, 1);
	dkCmdBufPushConstants(g_cmdbuf, dkMemBlockGetGpuAddr(g_unifMemBlock), dkMemBlockGetSize(g_unifMemBlock),
			      0, sizeof(m_vertexParams.projMatrix), m_vertexParams.projMatrix);

	g_vtxBufferHead = 0;
}

void CGSH_Deko3d::EndFrame()
{
	printf("g_vtxBufferHead: %d\n", g_vtxBufferHead);

	dkCmdBufBindVtxBuffer(g_cmdbuf, 0, dkMemBlockGetGpuAddr(g_vtxBufferMemBlock), dkMemBlockGetSize(g_vtxBufferMemBlock));
	dkCmdBufDraw(g_cmdbuf, DkPrimitive_Triangles, g_vtxBufferHead, 1, 0, 0);

	g_cmdsRender = dkCmdBufFinishList(g_cmdbuf);

	// Acquire a framebuffer from the swapchain (and wait for it to be available)
	int slot = dkQueueAcquireImage(g_renderQueue, g_swapchain);

	// Run the command list that binds said framebuffer as a render target
	dkQueueSubmitCommands(g_renderQueue, g_cmdsBindFramebuffer[slot]);

	// Run the main rendering command list
	dkQueueSubmitCommands(g_renderQueue, g_cmdsRender);

	// Now that we are done rendering, present it to the screen
	dkQueuePresentImage(g_renderQueue, g_swapchain, slot);
}

void CGSH_Deko3d::MarkNewFrame()
{
	printf("CGSH_Deko3d::MarkNewFrame\n");

	EndFrame();
	BeginFrame();

	CGSHandler::MarkNewFrame();
}

void CGSH_Deko3d::FlipImpl()
{
	printf("CGSH_Deko3d::FlipImpl\n");

	CGSHandler::FlipImpl();
}

void CGSH_Deko3d::SetupDepthBuffer(uint64 zbufReg, uint64 frameReg)
{
	auto frame = make_convertible<FRAME>(frameReg);
	auto zbuf = make_convertible<ZBUF>(zbufReg);

	switch (CGsPixelFormats::GetPsmPixelSize(zbuf.nPsm)) {
	case 16:
		m_nMaxZ = 32768.0f;
		break;
	case 24:
		m_nMaxZ = 8388608.0f;
		break;
	default:
	case 32:
		m_nMaxZ = 2147483647.0f;
		break;
	}
}

void CGSH_Deko3d::SetupTestFunctions(uint64 testReg)
{
	DkDepthStencilState depthStencilState;

	dkDepthStencilStateDefaults(&depthStencilState);

	auto tst = make_convertible<TEST>(testReg);

	if (tst.nDepthEnabled) {
		DkCompareOp depthFunc = DkCompareOp_Never;

		switch(tst.nDepthMethod) {
		case DEPTH_TEST_NEVER:
			depthFunc = DkCompareOp_Never;
			break;
		case DEPTH_TEST_ALWAYS:
			depthFunc = DkCompareOp_Always;
			break;
		case DEPTH_TEST_GEQUAL:
			depthFunc = DkCompareOp_Gequal;
			break;
		case DEPTH_TEST_GREATER:
			depthFunc = DkCompareOp_Greater;
			break;
		}
		depthStencilState.depthCompareOp = depthFunc;
		depthStencilState.depthTestEnable = true;
	} else {
		depthStencilState.depthTestEnable = false;
	}

	dkCmdBufBindDepthStencilState(g_cmdbuf, &depthStencilState);
}

void CGSH_Deko3d::SetRenderingContext(uint64 primReg)
{
	auto prim = make_convertible<PRMODE>(primReg);

	unsigned int context = prim.nContext;

	auto offset = make_convertible<XYOFFSET>(m_nReg[GS_REG_XYOFFSET_1 + context]);
	auto frame = make_convertible<FRAME>(m_nReg[GS_REG_FRAME_1 + context]);
	auto zbuf = make_convertible<ZBUF>(m_nReg[GS_REG_ZBUF_1 + context]);
	auto tex0 = make_convertible<TEX0>(m_nReg[GS_REG_TEX0_1 + context]);
	auto tex1 = make_convertible<TEX1>(m_nReg[GS_REG_TEX1_1 + context]);
	auto clamp = make_convertible<CLAMP>(m_nReg[GS_REG_CLAMP_1 + context]);
	auto alpha = make_convertible<ALPHA>(m_nReg[GS_REG_ALPHA_1 + context]);
	auto scissor = make_convertible<SCISSOR>(m_nReg[GS_REG_SCISSOR_1 + context]);
	auto test = make_convertible<TEST>(m_nReg[GS_REG_TEST_1 + context]);
	auto texA = make_convertible<TEXA>(m_nReg[GS_REG_TEXA]);
	auto fogCol = make_convertible<FOGCOL>(m_nReg[GS_REG_FOGCOL]);
	auto scanMask = m_nReg[GS_REG_SCANMSK] & 3;
	auto colClamp = m_nReg[GS_REG_COLCLAMP] & 1;
	auto fba = m_nReg[GS_REG_FBA_1 + context] & 1;

	SetupDepthBuffer(zbuf, frame);
	SetupTestFunctions(test);

	MakeLinearZOrtho(m_vertexParams.projMatrix, 0, frame.GetWidth(), 0, 480);

	m_fbBasePtr = frame.GetBasePtr();

	m_primOfsX = offset.GetX();
	m_primOfsY = offset.GetY();

	m_texWidth = tex0.GetWidth();
	m_texHeight = tex0.GetHeight();
}

void CGSH_Deko3d::VertexKick(uint8 registerId, uint64 data)
{
	//printf("CGSH_Deko3d::VertexKick\n");

	if (m_vtxCount == 0)
		return;

	bool drawingKick = (registerId == GS_REG_XYZ2) || (registerId == GS_REG_XYZF2);
	bool fog = (registerId == GS_REG_XYZF2) || (registerId == GS_REG_XYZF3);

	if (!m_drawEnabled)
		drawingKick = false;

	if (fog) {
		m_vtxBuffer[m_vtxCount - 1].position = data & 0x00FFFFFFFFFFFFFFULL;
		m_vtxBuffer[m_vtxCount - 1].rgbaq = m_nReg[GS_REG_RGBAQ];
		m_vtxBuffer[m_vtxCount - 1].uv = m_nReg[GS_REG_UV];
		m_vtxBuffer[m_vtxCount - 1].st = m_nReg[GS_REG_ST];
		m_vtxBuffer[m_vtxCount - 1].fog = static_cast<uint8>(data >> 56);
	} else {
		m_vtxBuffer[m_vtxCount - 1].position = data;
		m_vtxBuffer[m_vtxCount - 1].rgbaq = m_nReg[GS_REG_RGBAQ];
		m_vtxBuffer[m_vtxCount - 1].uv = m_nReg[GS_REG_UV];
		m_vtxBuffer[m_vtxCount - 1].st = m_nReg[GS_REG_ST];
		m_vtxBuffer[m_vtxCount - 1].fog = static_cast<uint8>(m_nReg[GS_REG_FOG] >> 56);
	}

	m_vtxCount--;

	if (m_vtxCount == 0) {
		if((m_nReg[GS_REG_PRMODECONT] & 1) != 0)
			m_primitiveMode <<= m_nReg[GS_REG_PRIM];
		else
			m_primitiveMode <<= m_nReg[GS_REG_PRMODE];

		if(drawingKick)
			SetRenderingContext(m_primitiveMode);

		switch(m_primitiveType) {
		case PRIM_POINT:
			if (drawingKick) Prim_Point();
			m_vtxCount = 1;
			break;
		case PRIM_LINE:
			if (drawingKick) Prim_Line();
			m_vtxCount = 2;
			break;
		case PRIM_LINESTRIP:
			if (drawingKick) Prim_Line();
			memcpy(&m_vtxBuffer[1], &m_vtxBuffer[0], sizeof(VERTEX));
			m_vtxCount = 1;
			break;
		case PRIM_TRIANGLE:
			if (drawingKick) Prim_Triangle();
			m_vtxCount = 3;
			break;
		case PRIM_TRIANGLESTRIP:
			if (drawingKick) Prim_Triangle();
			memcpy(&m_vtxBuffer[2], &m_vtxBuffer[1], sizeof(VERTEX));
			memcpy(&m_vtxBuffer[1], &m_vtxBuffer[0], sizeof(VERTEX));
			m_vtxCount = 1;
			break;
		case PRIM_TRIANGLEFAN:
			if (drawingKick) Prim_Triangle();
			memcpy(&m_vtxBuffer[1], &m_vtxBuffer[0], sizeof(VERTEX));
			m_vtxCount = 1;
			break;
		case PRIM_SPRITE:
			if (drawingKick) Prim_Sprite();
			m_vtxCount = 2;
			break;
		}
	}
}

float CGSH_Deko3d::CalcZ(float nZ)
{
	if (nZ < 256.0f)
		return nZ / 32768.0f;
	else if (nZ > m_nMaxZ)
		return 1.0f;
	else
		return nZ / m_nMaxZ;
}

void CGSH_Deko3d::Prim_Point()
{
	auto xyz = make_convertible<XYZ>(m_vtxBuffer[0].position);
	auto rgbaq = make_convertible<RGBAQ>(m_vtxBuffer[0].rgbaq);

	float x = xyz.GetX();
	float y = xyz.GetY();
	uint32 z = xyz.nZ;

	x -= m_primOfsX;
	y -= m_primOfsY;
#if 0
	auto color = MakeColor(
	    rgbaq.nR, rgbaq.nG,
	    rgbaq.nB, rgbaq.nA);

	// clang-format off
	CDraw::PRIM_VERTEX vertex =
	{
		//x, y, z, color, s, t, q, f
		  x, y, z, color, 0, 0, 1, 0,
	};
	// clang-format on

	//m_draw->AddVertices(&vertex, &vertex + 1);
#endif
}

void CGSH_Deko3d::Prim_Line()
{
	XYZ pos[2];
	pos[0] <<= m_vtxBuffer[1].position;
	pos[1] <<= m_vtxBuffer[0].position;

	float x1 = pos[0].GetX(), x2 = pos[1].GetX();
	float y1 = pos[0].GetY(), y2 = pos[1].GetY();
	uint32 z1 = pos[0].nZ, z2 = pos[1].nZ;

	RGBAQ rgbaq[2];
	rgbaq[0] <<= m_vtxBuffer[1].rgbaq;
	rgbaq[1] <<= m_vtxBuffer[0].rgbaq;

	x1 -= m_primOfsX;
	x2 -= m_primOfsX;

	y1 -= m_primOfsY;
	y2 -= m_primOfsY;

	float s[2] = {0, 0};
	float t[2] = {0, 0};
	float q[2] = {1, 1};

	if(m_primitiveMode.nTexture)
	{
		if(m_primitiveMode.nUseUV)
		{
			UV uv[3];
			uv[0] <<= m_vtxBuffer[1].uv;
			uv[1] <<= m_vtxBuffer[0].uv;

			s[0] = uv[0].GetU() / static_cast<float>(m_texWidth);
			s[1] = uv[1].GetU() / static_cast<float>(m_texWidth);

			t[0] = uv[0].GetV() / static_cast<float>(m_texHeight);
			t[1] = uv[1].GetV() / static_cast<float>(m_texHeight);

			//m_lastLineU = s[0];
			//m_lastLineV = t[0];
		}
		else
		{
			ST st[2];
			st[0] <<= m_vtxBuffer[1].st;
			st[1] <<= m_vtxBuffer[0].st;

			s[0] = st[0].nS;
			s[1] = st[1].nS;
			t[0] = st[0].nT;
			t[1] = st[1].nT;

			q[0] = rgbaq[0].nQ;
			q[1] = rgbaq[1].nQ;
		}
	}

#if 0
	auto color1 = MakeColor(
	    rgbaq[0].nR, rgbaq[0].nG,
	    rgbaq[0].nB, rgbaq[0].nA);

	auto color2 = MakeColor(
	    rgbaq[1].nR, rgbaq[1].nG,
	    rgbaq[1].nB, rgbaq[1].nA);

	// clang-format off
	CDraw::PRIM_VERTEX vertices[] =
	{
		{	x1, y1, z1, color1, s[0], t[0], q[0], 0 },
		{	x2, y2, z2, color2, s[1], t[1], q[1], 0 },
	};
	// clang-format on

	//m_draw->AddVertices(std::begin(vertices), std::end(vertices));
#endif
}

void CGSH_Deko3d::Prim_Triangle()
{
	XYZ pos[3];
	pos[0] <<= m_vtxBuffer[2].position;
	pos[1] <<= m_vtxBuffer[1].position;
	pos[2] <<= m_vtxBuffer[0].position;

	float x1 = pos[0].GetX(), x2 = pos[1].GetX(), x3 = pos[2].GetX();
	float y1 = pos[0].GetY(), y2 = pos[1].GetY(), y3 = pos[2].GetY();
	uint32 z1 = pos[0].nZ, z2 = pos[1].nZ, z3 = pos[2].nZ;

	RGBAQ rgbaq[3];
	rgbaq[0] <<= m_vtxBuffer[2].rgbaq;
	rgbaq[1] <<= m_vtxBuffer[1].rgbaq;
	rgbaq[2] <<= m_vtxBuffer[0].rgbaq;

	x1 -= m_primOfsX;
	x2 -= m_primOfsX;
	x3 -= m_primOfsX;

	y1 -= m_primOfsY;
	y2 -= m_primOfsY;
	y3 -= m_primOfsY;

	float s[3] = {0, 0, 0};
	float t[3] = {0, 0, 0};
	float q[3] = {1, 1, 1};

	float f[3] = {0, 0, 0};

	if(m_primitiveMode.nFog)
	{
		f[0] = static_cast<float>(0xFF - m_vtxBuffer[2].fog) / 255.0f;
		f[1] = static_cast<float>(0xFF - m_vtxBuffer[1].fog) / 255.0f;
		f[2] = static_cast<float>(0xFF - m_vtxBuffer[0].fog) / 255.0f;
	}

	if(m_primitiveMode.nTexture)
	{
		if(m_primitiveMode.nUseUV)
		{
			UV uv[3];
			uv[0] <<= m_vtxBuffer[2].uv;
			uv[1] <<= m_vtxBuffer[1].uv;
			uv[2] <<= m_vtxBuffer[0].uv;

			s[0] = uv[0].GetU() / static_cast<float>(m_texWidth);
			s[1] = uv[1].GetU() / static_cast<float>(m_texWidth);
			s[2] = uv[2].GetU() / static_cast<float>(m_texWidth);

			t[0] = uv[0].GetV() / static_cast<float>(m_texHeight);
			t[1] = uv[1].GetV() / static_cast<float>(m_texHeight);
			t[2] = uv[2].GetV() / static_cast<float>(m_texHeight);
		}
		else
		{
			ST st[3];
			st[0] <<= m_vtxBuffer[2].st;
			st[1] <<= m_vtxBuffer[1].st;
			st[2] <<= m_vtxBuffer[0].st;

			s[0] = st[0].nS;
			s[1] = st[1].nS;
			s[2] = st[2].nS;
			t[0] = st[0].nT;
			t[1] = st[1].nT;
			t[2] = st[2].nT;

			q[0] = rgbaq[0].nQ;
			q[1] = rgbaq[1].nQ;
			q[2] = rgbaq[2].nQ;
		}
	}

	auto color1 = MakeColor(
	    rgbaq[0].nR, rgbaq[0].nG,
	    rgbaq[0].nB, rgbaq[0].nA);

	auto color2 = MakeColor(
	    rgbaq[1].nR, rgbaq[1].nG,
	    rgbaq[1].nB, rgbaq[1].nA);

	auto color3 = MakeColor(
	    rgbaq[2].nR, rgbaq[2].nG,
	    rgbaq[2].nB, rgbaq[2].nA);

	if (m_primitiveMode.nShading == 0) {
		//Flat shaded triangles use the last color set
		color1 = color2 = color3;
	}

	printf("Triangle {%f %f %f}, {%f %f %f}, {%f %f %f}\n", x1, y1, CalcZ(z1), x2, y2, CalcZ(z2), x3, y3, CalcZ(z3));

	Vertex *vertices = ((Vertex *)dkMemBlockGetCpuAddr(g_vtxBufferMemBlock)) + g_vtxBufferHead;
	vertices[0] = {x1, y1, CalcZ(z1), color1};
	vertices[1] = {x2, y2, CalcZ(z2), color2};
	vertices[2] = {x3, y3, CalcZ(z3), color3};
	g_vtxBufferHead += 3;

}

void CGSH_Deko3d::Prim_Sprite()
{
	XYZ pos[2];
	pos[0] <<= m_vtxBuffer[1].position;
	pos[1] <<= m_vtxBuffer[0].position;

	float x1 = pos[0].GetX(), y1 = pos[0].GetY();
	float x2 = pos[1].GetX(), y2 = pos[1].GetY();
	uint32 z = pos[1].nZ;

	RGBAQ rgbaq[2];
	rgbaq[0] <<= m_vtxBuffer[1].rgbaq;
	rgbaq[1] <<= m_vtxBuffer[0].rgbaq;

	x1 -= m_primOfsX;
	x2 -= m_primOfsX;

	y1 -= m_primOfsY;
	y2 -= m_primOfsY;

	float s[2] = {0, 0};
	float t[2] = {0, 0};

	if(m_primitiveMode.nTexture)
	{
		if(m_primitiveMode.nUseUV)
		{
			UV uv[2];
			uv[0] <<= m_vtxBuffer[1].uv;
			uv[1] <<= m_vtxBuffer[0].uv;

			s[0] = uv[0].GetU() / static_cast<float>(m_texWidth);
			s[1] = uv[1].GetU() / static_cast<float>(m_texWidth);

			t[0] = uv[0].GetV() / static_cast<float>(m_texHeight);
			t[1] = uv[1].GetV() / static_cast<float>(m_texHeight);
		}
		else
		{
			ST st[2];

			st[0] <<= m_vtxBuffer[1].st;
			st[1] <<= m_vtxBuffer[0].st;

			float q1 = rgbaq[1].nQ;
			float q2 = rgbaq[0].nQ;
			if(q1 == 0) q1 = 1;
			if(q2 == 0) q2 = 1;

			s[0] = st[0].nS / q1;
			s[1] = st[1].nS / q2;

			t[0] = st[0].nT / q1;
			t[1] = st[1].nT / q2;
		}
	}
#if 0
	auto color = MakeColor(
	    rgbaq[1].nR, rgbaq[1].nG,
	    rgbaq[1].nB, rgbaq[1].nA);

	// clang-format off
	CDraw::PRIM_VERTEX vertices[] =
	{
		{x1, y1, z, color, s[0], t[0], 1, 0},
		{x2, y1, z, color, s[1], t[0], 1, 0},
		{x1, y2, z, color, s[0], t[1], 1, 0},

		{x1, y2, z, color, s[0], t[1], 1, 0},
		{x2, y1, z, color, s[1], t[0], 1, 0},
		{x2, y2, z, color, s[1], t[1], 1, 0},
	};
	// clang-format on

	{
		const auto topLeftCorner = vertices;
		const auto bottomRightCorner = vertices + 5;
		CGsSpriteRect rect(topLeftCorner->x, topLeftCorner->y, bottomRightCorner->x, bottomRightCorner->y);
		CheckSpriteCachedClutInvalidation(rect);
	}

	//m_draw->AddVertices(std::begin(vertices), std::end(vertices));
#endif
}

void CGSH_Deko3d::WriteRegisterImpl(uint8 registerId, uint64 data)
{
	//printf("CGSH_Deko3d::WriteRegisterImpl\n");

	CGSHandler::WriteRegisterImpl(registerId, data);

	switch (registerId) {
	case GS_REG_PRIM:
		m_primitiveType = static_cast<unsigned int>(data & 0x07);
		switch (m_primitiveType) {
		case PRIM_POINT:
			m_vtxCount = 1;
			break;
		case PRIM_LINE:
		case PRIM_LINESTRIP:
			m_vtxCount = 2;
			break;
		case PRIM_TRIANGLE:
		case PRIM_TRIANGLESTRIP:
		case PRIM_TRIANGLEFAN:
			m_vtxCount = 3;
			break;
		case PRIM_SPRITE:
			m_vtxCount = 2;
			break;
		}
		break;
	case GS_REG_XYZ2:
	case GS_REG_XYZ3:
	case GS_REG_XYZF2:
	case GS_REG_XYZF3:
		VertexKick(registerId, data);
		break;
	}	
}

void CGSH_Deko3d::ProcessHostToLocalTransfer()
{
	//printf("CGSH_Deko3d::ProcessHostToLocalTransfer()\n");
}

void CGSH_Deko3d::ProcessLocalToHostTransfer()
{
	//printf("CGSH_Deko3d::ProcessLocalToHostTransfer()\n");
}

void CGSH_Deko3d::ProcessLocalToLocalTransfer()
{
	//printf("CGSH_Deko3d::ProcessLocalToLocalTransfer()\n");
}

void CGSH_Deko3d::ProcessClutTransfer(uint32, uint32)
{
	//printf("CGSH_Deko3d::ProcessClutTransfer()\n");
}

CGSHandler::FactoryFunction CGSH_Deko3d::GetFactoryFunction()
{
	return []() { return new CGSH_Deko3d(); };
}

void CGSH_Deko3d::MakeLinearZOrtho(float* matrix, float left, float right, float bottom, float top)
{
	matrix[0] = 2.0f / (right - left);
	matrix[1] = 0;
	matrix[2] = 0;
	matrix[3] = 0;

	matrix[4] = 0;
	matrix[5] = -2.0f / (top - bottom);
	matrix[6] = 0;
	matrix[7] = 0;

	matrix[8] = 0;
	matrix[9] = 0;
	matrix[10] = 1;
	matrix[11] = 0;

	matrix[12] = -(right + left) / (right - left);
	matrix[13] = (top + bottom) / (top - bottom);
	matrix[14] = 0;
	matrix[15] = 1;
}