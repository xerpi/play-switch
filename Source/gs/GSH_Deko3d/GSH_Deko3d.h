#pragma once

#include "../GSHandler.h"
#include "../../gs/GsDebuggerInterface.h"
#include "../../gs/GsTextureCache.h"

class CGSH_Deko3d : public CGSHandler
{
public:
	CGSH_Deko3d() = default;
	virtual ~CGSH_Deko3d() = default;

	void ProcessHostToLocalTransfer() override;
	void ProcessLocalToHostTransfer() override;
	void ProcessLocalToLocalTransfer() override;
	void ProcessClutTransfer(uint32, uint32) override;

	static FactoryFunction GetFactoryFunction();

private:
	//These need to match the layout of the shader's uniform block
	struct VERTEXPARAMS
	{
		float projMatrix[16];
	};

	void InitializeImpl() override;
	void ReleaseImpl() override;
	void MarkNewFrame() override;
	void FlipImpl() override;
	void WriteRegisterImpl(uint8, uint64) override;

	void BeginFrame();
	void EndFrame();

	void VertexKick(uint8, uint64);
	void SetRenderingContext(uint64);
	void SetupDepthBuffer(uint64, uint64);
	void SetupTestFunctions(uint64);

	void Prim_Point();
	void Prim_Line();
	void Prim_Triangle();
	void Prim_Sprite();

	float CalcZ(float);

	static void MakeLinearZOrtho(float *, float, float, float, float);

	/* Draw context */
	VERTEX m_vtxBuffer[3];
	uint32 m_vtxCount = 0;
	uint32 m_primitiveType = 0;
	PRMODE m_primitiveMode;
	uint32 m_fbBasePtr = 0;
	float m_primOfsX = 0;
	float m_primOfsY = 0;
	uint32 m_texWidth = 0;
	uint32 m_texHeight = 0;
	float m_nMaxZ;

	VERTEXPARAMS m_vertexParams;
};
