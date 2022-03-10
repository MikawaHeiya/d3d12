#include "d3d_application.h"
#include "framework.h"
#include "game_timer.h"
#include <WindowsX.h>
#include <string>

LRESULT CALLBACK
main_window_process(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param)
{
	// Forward hwnd on because we can get messages (e.g., WM_CREATE)
	// before CreateWindow returns, and thus before mhMainWnd is valid.
	return d3d_application::application()->message_process(hwnd, msg, w_param, l_param);
}

d3d_application* d3d_application::m_application = nullptr;
d3d_application* d3d_application::application()
{
	return m_application;
}

d3d_application::d3d_application(HINSTANCE hinstance) : m_application_instance(hinstance)
{
	assert(m_application == nullptr);
	m_application = this;
}

d3d_application::~d3d_application()
{
	if (m_d3d_device)
	{
		flush_command_queue();
	}
}

HINSTANCE d3d_application::application_instance() const
{
	return m_application_instance;
}

HWND d3d_application::main_window() const
{
	return m_main_window;
}

float d3d_application::aspect_ratio() const
{
	return static_cast<float>(m_client_width) / m_client_height;
}

bool d3d_application::get_4xMSAA_state() const
{
	return m_4xMSAA_enable;
}

void d3d_application::set_4xMSAA_state(bool state)
{
	if (m_4xMSAA_enable != state)
	{
		m_4xMSAA_enable = state;

		create_swap_chain();
		on_resize();
	}
}

int d3d_application::run()
{
	MSG msg{ 0 };

	m_timer.reset();

	while (msg.message != WM_QUIT)
	{
		// If there are Window messages then process them.
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else  // Otherwise, do animation/game stuff.
		{
			m_timer.tick();

			if (!m_application_paused)
			{
				calculate_frame_stats();
				update(m_timer);
				draw(m_timer);
			}
			else
			{
				Sleep(100);
			}
		}
	}

	return static_cast<int>(msg.wParam);
}

bool d3d_application::initialize()
{
	if (!initialize_main_window())
	{
		return false;
	}

	if (!initialize_direct3d())
	{
		return false;
	}

	on_resize();

	return true;
}

void d3d_application::create_rtv_dsv_descriptor_heaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc;
	rtv_heap_desc.NumDescriptors = SwapChainBufferCount;
	rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtv_heap_desc.NodeMask = 0;
	ThrowIfFailed(m_d3d_device->CreateDescriptorHeap(
		&rtv_heap_desc, IID_PPV_ARGS(m_rtv_heap.GetAddressOf())));

	D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc;
	dsv_heap_desc.NumDescriptors = 1;
	dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsv_heap_desc.NodeMask = 0;
	ThrowIfFailed(m_d3d_device->CreateDescriptorHeap(
		&dsv_heap_desc, IID_PPV_ARGS(m_dsv_heap.GetAddressOf())));
}

void d3d_application::on_resize()
{
	assert(m_d3d_device);
	assert(m_swap_chain);
	assert(m_direct_cmd_list_allocator);

	// Flush before changing any resources.
	flush_command_queue();

	ThrowIfFailed(m_command_list->Reset(m_direct_cmd_list_allocator.Get(), nullptr));

	// Release the previous resources we will be recreating.
	for (int i = 0; i < SwapChainBufferCount; ++i)
	{
		m_swap_chain_buffer[i].Reset();
	}
	m_depth_stencil_buffer.Reset();

	// Resize the swap chain.
	ThrowIfFailed(m_swap_chain->ResizeBuffers(
		SwapChainBufferCount,
		m_client_width, m_client_height,
		m_back_buffer_format,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

	m_current_back_buffer = 0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_heap_handle(
		m_rtv_heap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < SwapChainBufferCount; ++i)
	{
		ThrowIfFailed(m_swap_chain->GetBuffer(i, IID_PPV_ARGS(&m_swap_chain_buffer[i])));
		m_d3d_device->CreateRenderTargetView(
			m_swap_chain_buffer[i].Get(), nullptr, rtv_heap_handle);
		rtv_heap_handle.Offset(1, m_rtv_descriptor_size);
	}

	// Create the depth/stencil buffer and view.
	D3D12_RESOURCE_DESC depth_stencil_desc;
	depth_stencil_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depth_stencil_desc.Alignment = 0;
	depth_stencil_desc.Width = m_client_width;
	depth_stencil_desc.Height = m_client_height;
	depth_stencil_desc.DepthOrArraySize = 1;
	depth_stencil_desc.MipLevels = 1;

	// Correction 11/12/2016: SSAO chapter requires an SRV to the depth buffer to read from 
	// the depth buffer.  Therefore, because we need to create two views to the same resource:
	//   1. SRV format: DXGI_FORMAT_R24_UNORM_X8_TYPELESS
	//   2. DSV Format: DXGI_FORMAT_D24_UNORM_S8_UINT
	// we need to create the depth buffer resource with a typeless format.  
	depth_stencil_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;

	depth_stencil_desc.SampleDesc.Count = m_4xMSAA_enable ? 4 : 1;
	depth_stencil_desc.SampleDesc.Quality = m_4xMSAA_enable ? (m_4xMSAA_quanlity - 1) : 0;
	depth_stencil_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depth_stencil_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE opt_clear;
	opt_clear.Format = m_depth_stencil_buffer_format;
	opt_clear.DepthStencil.Depth = 1.0f;
	opt_clear.DepthStencil.Stencil = 0;
	auto hp1 = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(m_d3d_device->CreateCommittedResource(
		&hp1,
		D3D12_HEAP_FLAG_NONE,
		&depth_stencil_desc,
		D3D12_RESOURCE_STATE_COMMON,
		&opt_clear,
		IID_PPV_ARGS(m_depth_stencil_buffer.GetAddressOf())));

	// Create descriptor to mip level 0 of entire resource using the format of the resource.
	D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
	dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
	dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsv_desc.Format = m_depth_stencil_buffer_format;
	dsv_desc.Texture2D.MipSlice = 0;
	m_d3d_device->CreateDepthStencilView(
		m_depth_stencil_buffer.Get(), &dsv_desc, depth_stencil_view());

	// Transition the resource from its initial state to be used as a depth buffer.
	auto tr1 = CD3DX12_RESOURCE_BARRIER::Transition(m_depth_stencil_buffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	m_command_list->ResourceBarrier(1, &tr1);

	// Execute the resize commands.
	ThrowIfFailed(m_command_list->Close());
	ID3D12CommandList* cmds_lists[] = { m_command_list.Get() };
	m_command_queue->ExecuteCommandLists(_countof(cmds_lists), cmds_lists);

	// Wait until resize is complete.
	flush_command_queue();

	// Update the viewport transform to cover the client area.
	m_screen_viewport.TopLeftX = 0;
	m_screen_viewport.TopLeftY = 0;
	m_screen_viewport.Width = static_cast<float>(m_client_width);
	m_screen_viewport.Height = static_cast<float>(m_client_height);
	m_screen_viewport.MinDepth = 0.0f;
	m_screen_viewport.MaxDepth = 1.0f;

	m_scissor_rect = { 0, 0, m_client_width, m_client_height };
}

LRESULT d3d_application::message_process(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param)
{
	switch (msg)
	{
		// WM_ACTIVATE is sent when the window is activated or deactivated.  
		// We pause the game when the window is deactivated and unpause it 
		// when it becomes active.  
	case WM_ACTIVATE:
		if (LOWORD(w_param) == WA_INACTIVE)
		{
			m_application_paused = true;
			m_timer.stop();
		}
		else
		{
			m_application_paused = false;
			m_timer.start();
		}
		return 0;

		// WM_SIZE is sent when the user resizes the window.  
	case WM_SIZE:
		// Save the new client area dimensions.
		m_client_width = LOWORD(l_param);
		m_client_height = HIWORD(l_param);
		if (m_d3d_device)
		{
			if (w_param == SIZE_MINIMIZED)
			{
				m_application_paused = true;
				m_minimized = true;
				m_maximized = false;
			}
			else if (w_param == SIZE_MAXIMIZED)
			{
				m_application_paused = false;
				m_minimized = false;
				m_maximized = true;
				on_resize();
			}
			else if (w_param == SIZE_RESTORED)
			{

				// Restoring from minimized state?
				if (m_minimized)
				{
					m_application_paused = false;
					m_minimized = false;
					on_resize();
				}

				// Restoring from maximized state?
				else if (m_maximized)
				{
					m_application_paused = false;
					m_maximized = false;
					on_resize();
				}
				else if (m_resizing)
				{
					// If user is dragging the resize bars, we do not resize 
					// the buffers here because as the user continuously 
					// drags the resize bars, a stream of WM_SIZE messages are
					// sent to the window, and it would be pointless (and slow)
					// to resize for each WM_SIZE message received from dragging
					// the resize bars.  So instead, we reset after the user is 
					// done resizing the window and releases the resize bars, which 
					// sends a WM_EXITSIZEMOVE message.
				}
				else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
				{
					on_resize();
				}
			}
		}
		return 0;

		// WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
	case WM_ENTERSIZEMOVE:
		m_application_paused = true;
		m_resizing = true;
		m_timer.stop();
		return 0;

		// WM_EXITSIZEMOVE is sent when the user releases the resize bars.
		// Here we reset everything based on the new window dimensions.
	case WM_EXITSIZEMOVE:
		m_application_paused = false;
		m_resizing = false;
		m_timer.start();
		on_resize();
		return 0;

		// WM_DESTROY is sent when the window is being destroyed.
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

		// The WM_MENUCHAR message is sent when a menu is active and the user presses 
		// a key that does not correspond to any mnemonic or accelerator key. 
	case WM_MENUCHAR:
		// Don't beep when we alt-enter.
		return MAKELRESULT(0, MNC_CLOSE);

		// Catch this message so to prevent the window from becoming too small.
	case WM_GETMINMAXINFO:
		((MINMAXINFO*)l_param)->ptMinTrackSize.x = 200;
		((MINMAXINFO*)l_param)->ptMinTrackSize.y = 200;
		return 0;

	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		on_mouse_down(w_param, GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
		return 0;
	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		on_mouse_up(w_param, GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
		return 0;
	case WM_MOUSEMOVE:
		on_mouse_move(w_param, GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
		return 0;
	case WM_KEYUP:
		if (w_param == VK_ESCAPE)
		{
			PostQuitMessage(0);
		}
		else if ((int)w_param == VK_F2)
			set_4xMSAA_state(!m_4xMSAA_enable);

		return 0;
	}

	return DefWindowProc(hwnd, msg, w_param, l_param);
}

bool d3d_application::initialize_main_window()
{
	WNDCLASS wc;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = main_window_process;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = m_application_instance;
	wc.hIcon = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszMenuName = 0;
	wc.lpszClassName = L"MainWnd";

	if (!RegisterClass(&wc))
	{
		MessageBox(0, L"RegisterClass Failed.", 0, 0);
		return false;
	}

	// Compute window rectangle dimensions based on requested client area dimensions.
	RECT R = { 0, 0, m_client_width, m_client_height };
	AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	int width = R.right - R.left;
	int height = R.bottom - R.top;

	m_main_window = CreateWindow(L"MainWnd", m_main_window_caption.c_str(),
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, 0, 0, m_application_instance, 0);
	if (!m_main_window)
	{
		MessageBox(0, L"CreateWindow Failed.", 0, 0);
		return false;
	}

	ShowWindow(m_main_window, SW_SHOW);
	UpdateWindow(m_main_window);

	return true;
}

bool d3d_application::initialize_direct3d()
{
#if defined(DEBUG) || defined(_DEBUG) 
	// Enable the D3D12 debug layer.
	{
		ComPtr<ID3D12Debug> debug_controller;
		ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)));
		debug_controller->EnableDebugLayer();
	}
#endif

	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgi_factory)));

	// Try to create hardware device.
	HRESULT hardwareResult = D3D12CreateDevice(
		nullptr,             // default adapter
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&m_d3d_device));

	// Fallback to WARP device.
	if (FAILED(hardwareResult))
	{
		ComPtr<IDXGIAdapter> p_warp_adapter;
		ThrowIfFailed(m_dxgi_factory->EnumWarpAdapter(IID_PPV_ARGS(&p_warp_adapter)));

		ThrowIfFailed(D3D12CreateDevice(
			p_warp_adapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_d3d_device)));
	}

	ThrowIfFailed(m_d3d_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&m_fence)));

	m_rtv_descriptor_size = m_d3d_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_dsv_descriptor_size = m_d3d_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_cbv_srv_uav_descriptor_size = m_d3d_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Check 4X MSAA quality support for our back buffer format.
	// All Direct3D 11 capable devices support 4X MSAA for all render 
	// target formats, so we only need to check quality support.

	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ms_quality_levels;
	ms_quality_levels.Format = m_back_buffer_format;
	ms_quality_levels.SampleCount = 4;
	ms_quality_levels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	ms_quality_levels.NumQualityLevels = 0;
	ThrowIfFailed(m_d3d_device->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&ms_quality_levels,
		sizeof(ms_quality_levels)));

	m_4xMSAA_quanlity = ms_quality_levels.NumQualityLevels;
	assert(m_4xMSAA_quanlity > 0 && "Unexpected MSAA quality level.");

#ifdef _DEBUG
	log_adapters();
#endif

	create_command_objects();
	create_swap_chain();
	create_rtv_dsv_descriptor_heaps();

	return true;
}

void d3d_application::create_command_objects()
{
	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(m_d3d_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&m_command_queue)));

	ThrowIfFailed(m_d3d_device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(m_direct_cmd_list_allocator.GetAddressOf())));

	ThrowIfFailed(m_d3d_device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_direct_cmd_list_allocator.Get(), // Associated command allocator
		nullptr,                   // Initial PipelineStateObject
		IID_PPV_ARGS(m_command_list.GetAddressOf())));

	// Start off in a closed state.  This is because the first time we refer 
	// to the command list we will Reset it, and it needs to be closed before
	// calling Reset.
	m_command_list->Close();
}

void d3d_application::create_swap_chain()
{
	// Release the previous swapchain we will be recreating.
	m_swap_chain.Reset();

	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width = m_client_width;
	sd.BufferDesc.Height = m_client_height;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = m_back_buffer_format;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.SampleDesc.Count = m_4xMSAA_enable ? 4 : 1;
	sd.SampleDesc.Quality = m_4xMSAA_enable ? (m_4xMSAA_enable - 1) : 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = SwapChainBufferCount;
	sd.OutputWindow = m_main_window;
	sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	// Note: Swap chain uses queue to perform flush.
	ThrowIfFailed(m_dxgi_factory->CreateSwapChain(
		m_command_queue.Get(),
		&sd,
		m_swap_chain.GetAddressOf()));
}

void d3d_application::flush_command_queue()
{
	// Advance the fence value to mark commands up to this fence point.
	++m_current_fence;

	// Add an instruction to the command queue to set a new fence point.  Because we 
	// are on the GPU timeline, the new fence point won't be set until the GPU finishes
	// processing all the commands prior to this Signal().
	ThrowIfFailed(m_command_queue->Signal(m_fence.Get(), m_current_fence));

	// Wait until the GPU has completed commands up to this fence point.
	if (m_fence->GetCompletedValue() < m_current_fence)
	{
		HANDLE event_handle = CreateEventEx(nullptr, nullptr, FALSE, EVENT_ALL_ACCESS);

		// Fire event when GPU hits current fence.  
		ThrowIfFailed(m_fence->SetEventOnCompletion(m_current_fence, event_handle));

		// Wait until the GPU hits current fence event is fired.
		WaitForSingleObject(event_handle, INFINITE);
		
		CloseHandle(event_handle);
	}
}

void d3d_application::calculate_frame_stats()
{
	static int frame_count = 0;
	static float time_elapsed = .0f;

	++frame_count;

	if (m_timer.total_time() - time_elapsed >= 1.0f)
	{
		auto fps = static_cast<float>(frame_count); // fps = frameCnt / 1
		auto mspf = 1000.0f / fps;

		std::wstring window_text = m_main_window_caption +
			L"    fps: " + std::to_wstring(fps) +
			L"   mspf: " + std::to_wstring(mspf);

		SetWindowText(m_main_window, window_text.c_str());

		// Reset for next average.
		frame_count = 0;
		time_elapsed += 1.0f;
	}
}

void d3d_application::log_adapters()
{
	UINT i = 0;
	IDXGIAdapter* adapter = nullptr;
	std::vector<IDXGIAdapter*> adapter_list;
	while (m_dxgi_factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC desc;
		adapter->GetDesc(&desc);

		std::wstring text = L"***Adapter: ";
		text += desc.Description;
		text += L"\n";

		OutputDebugString(text.c_str());

		adapter_list.push_back(adapter);

		++i;
	}

	for (size_t i = 0; i < adapter_list.size(); ++i)
	{
		log_adapter_outputs(adapter_list[i]);
		ReleaseCom(adapter_list[i]);
	}
}

void d3d_application::log_adapter_outputs(IDXGIAdapter* adapter)
{
	UINT i = 0;
	IDXGIOutput* output = nullptr;
	while (adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_OUTPUT_DESC desc;
		output->GetDesc(&desc);

		std::wstring text = L"***Output: ";
		text += desc.DeviceName;
		text += L"\n";
		OutputDebugString(text.c_str());

		log_output_display_mode(output, m_back_buffer_format);

		ReleaseCom(output);

		++i;
	}
}

void d3d_application::log_output_display_mode(IDXGIOutput* output, DXGI_FORMAT format)
{
	UINT count = 0;
	UINT flags = 0;

	// Call with nullptr to get list count.
	output->GetDisplayModeList(format, flags, &count, nullptr);

	std::vector<DXGI_MODE_DESC> modeList(count);
	output->GetDisplayModeList(format, flags, &count, &modeList[0]);

	for (auto& x : modeList)
	{
		UINT n = x.RefreshRate.Numerator;
		UINT d = x.RefreshRate.Denominator;
		std::wstring text =
			L"Width = " + std::to_wstring(x.Width) + L" " +
			L"Height = " + std::to_wstring(x.Height) + L" " +
			L"Refresh = " + std::to_wstring(n) + L"/" + std::to_wstring(d) +
			L"\n";

		::OutputDebugString(text.c_str());
	}
}
