#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <switch.h>

#include "Log.h"
#include "AppConfig.h"
#include "PS2VM.h"
#include "DiskUtils.h"
#include "PH_Generic.h"
#include "GSH_Deko3d.h"

#include "PS2VM_Preferences.h"

#define DEFAULT_FILE 	"/switch/Play/test.elf"

#define EXIT_COMBO (HidNpadButton_Plus | HidNpadButton_R)

extern "C" {

long pathconf(const char *path, int name)
{
	switch (name) {
	case _PC_PATH_MAX:
		return PATH_MAX;
	default:
		return 0;
	}
}

}

#define JIT_SIZE	(16 * 1024 * 1024)
static Jit jit;
static size_t jit_offset = 0;

void switch_jit_init()
{
	Result res = jitCreate(&jit, JIT_SIZE);
	assert(R_SUCCEEDED(res));
}

void switch_jit_finish()
{
	Result res = jitClose(&jit);
	assert(R_SUCCEEDED(res));
}

void switch_jit_alloc(size_t size, void **rw_addr, void **rx_addr)
{
        *rw_addr = (u8 *)jitGetRwAddr(&jit) + jit_offset;
        *rx_addr = (u8 *)jitGetRxAddr(&jit) + jit_offset;
	jit_offset += size;
}

void switch_jit_transition_to_writable()
{
	Result res = jitTransitionToWritable(&jit);
	assert(R_SUCCEEDED(res));
}

void switch_jit_transition_to_executable()
{
	Result res = jitTransitionToExecutable(&jit);
	assert(R_SUCCEEDED(res));
}

static bool IsBootableExecutablePath(const fs::path& filePath)
{
	auto extension = filePath.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
	return (extension == ".elf");
}

static bool IsBootableDiscImagePath(const fs::path& filePath)
{
	const auto& supportedExtensions = DiskUtils::GetSupportedExtensions();
	auto extension = filePath.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
	auto extensionIterator = supportedExtensions.find(extension);
	return extensionIterator != std::end(supportedExtensions);
}

int main(int argc, char **argv)
{
	CPS2VM *m_virtualMachine = nullptr;
	bool executionOver = false;
	const char *file = DEFAULT_FILE;
	fs::path filePath(file);

	socketInitializeDefault();
	nxlinkStdio();
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	PadState pad;
	padInitializeDefault(&pad);

	printf("Play! " PLAY_VERSION "\n");

	switch_jit_init();

	m_virtualMachine = new CPS2VM();
	m_virtualMachine->Initialize();
	m_virtualMachine->CreatePadHandler(CPH_Generic::GetFactoryFunction());
	m_virtualMachine->CreateGSHandler(CGSH_Deko3d::GetFactoryFunction());

	auto connection = m_virtualMachine->m_ee->m_os->OnRequestExit.Connect(
		[&executionOver]() {
			executionOver = true;
		});

	printf("Loading: '%s'\n", file);
	try {
		if (IsBootableExecutablePath(filePath)) {
			m_virtualMachine->m_ee->m_os->BootFromFile(file);
		} else if (IsBootableDiscImagePath(filePath)) {
			CAppConfig::GetInstance().SetPreferencePath(PREF_PS2_CDROM0_PATH, file);
			CAppConfig::GetInstance().Save();
			m_virtualMachine->m_ee->m_os->BootFromCDROM();
		}
	} catch (const std::runtime_error& e) {
		printf("Exception: %s\n", e.what());
		goto done;
	} catch (...) {
		printf("Unknown exception\n");
		goto done;
	}

	m_virtualMachine->Resume();


	while (appletMainLoop() && !executionOver) {
		padUpdate(&pad);

		u64 buttons = padGetButtons(&pad);
		if ((buttons & EXIT_COMBO) == EXIT_COMBO)
			executionOver = true;

		auto padHandler = static_cast<CPH_Generic *>(m_virtualMachine->GetPadHandler());

		//padHandler->SetAxisState(PS2::CControllerInfo::ANALOG_LEFT_X, (pad.lx / 255.f) * 2.f - 1.f);
		//padHandler->SetAxisState(PS2::CControllerInfo::ANALOG_LEFT_Y, (pad.ly / 255.f) * 2.f - 1.f);
		//padHandler->SetAxisState(PS2::CControllerInfo::ANALOG_RIGHT_X, (pad.rx / 255.f) * 2.f - 1.f);
		//padHandler->SetAxisState(PS2::CControllerInfo::ANALOG_RIGHT_Y, (ry / 255.f) * 2.f - 1.f);
		padHandler->SetButtonState(PS2::CControllerInfo::DPAD_UP, buttons & HidNpadButton_Up);
		padHandler->SetButtonState(PS2::CControllerInfo::DPAD_DOWN, buttons & HidNpadButton_Down);
		padHandler->SetButtonState(PS2::CControllerInfo::DPAD_LEFT, buttons & HidNpadButton_Left);
		padHandler->SetButtonState(PS2::CControllerInfo::DPAD_RIGHT, buttons & HidNpadButton_Right);
		padHandler->SetButtonState(PS2::CControllerInfo::SELECT, buttons & HidNpadButton_Minus);
		padHandler->SetButtonState(PS2::CControllerInfo::START, buttons & HidNpadButton_Plus);
		padHandler->SetButtonState(PS2::CControllerInfo::SQUARE, buttons & HidNpadButton_Y);
		padHandler->SetButtonState(PS2::CControllerInfo::TRIANGLE, buttons & HidNpadButton_X);
		padHandler->SetButtonState(PS2::CControllerInfo::CIRCLE, buttons & HidNpadButton_A);
		padHandler->SetButtonState(PS2::CControllerInfo::CROSS, buttons & HidNpadButton_B);
		padHandler->SetButtonState(PS2::CControllerInfo::L1, buttons & HidNpadButton_L);
		padHandler->SetButtonState(PS2::CControllerInfo::R1, buttons & HidNpadButton_R);
	}

done:
	printf("Finish\n");

	if (m_virtualMachine) {
		m_virtualMachine->Pause();
		m_virtualMachine->DestroyPadHandler();
		m_virtualMachine->DestroyGSHandler();
		//m_virtualMachine->DestroySoundHandler();
		m_virtualMachine->Destroy();
		delete m_virtualMachine;
		m_virtualMachine = nullptr;
	}

	switch_jit_finish();

	socketExit();

	return 0;
}
