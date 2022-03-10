#include "d3d_box.h"

int WINAPI WinMain(HINSTANCE hinstance, HINSTANCE previous_instance,
	PSTR cmd_line, int show_cmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		d3d_box box{ hinstance };
		if (!box.initialize())
		{
			return 0;
		}

		return box.run();
	}
	catch (hresult_error const& error)
	{
		MessageBox(nullptr, error.what().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}
