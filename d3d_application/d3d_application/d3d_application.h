#pragma once
#include "framework.h"
#include "d3d_utility.h"
#include "game_timer.h"
#include <string>

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

class d3d_application
{
protected:
	d3d_application(HINSTANCE hinstance);
	d3d_application(d3d_application const&) = delete;
	d3d_application& operator=(d3d_application const&) = delete;
	virtual ~d3d_application();

	virtual void create_rtv_dsv_descriptor_heaps();
	virtual void update(game_timer const&) = 0;
	virtual void draw(game_timer const&) = 0;

	virtual void on_resize();
	virtual void on_mouse_up(WPARAM, int, int) {};
	virtual void on_mouse_down(WPARAM, int, int) {};
	virtual void on_mouse_move(WPARAM, int, int) {};

	bool initialize_main_window();
	bool initialize_direct3d();
	void create_command_objects();
	void create_swap_chain();

	void flush_command_queue();

	ID3D12Resource* current_back_buffer() const
	{
		return m_swap_chain_buffer[m_current_back_buffer].Get();
	}

	D3D12_CPU_DESCRIPTOR_HANDLE current_back_buffer_view() const
	{
		return CD3DX12_CPU_DESCRIPTOR_HANDLE(
			m_rtv_heap->GetCPUDescriptorHandleForHeapStart(),
			m_current_back_buffer, m_rtv_descriptor_size);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE depth_stencil_view() const
	{
		return m_dsv_heap->GetCPUDescriptorHandleForHeapStart();
	}

	void calculate_frame_stats();

	void log_adapters();
	void log_adapter_outputs(IDXGIAdapter*);
	void log_output_display_mode(IDXGIOutput*, DXGI_FORMAT);

	static d3d_application* m_application;

	HINSTANCE m_application_instance = nullptr;
	HWND m_main_window = nullptr;
	bool m_application_paused = false;
	bool m_minimized = false;
	bool m_maximized = false;
	bool m_resizing = false;
	bool m_full_screen = false;

	bool m_4xMSAA_enable = false;
	UINT m_4xMSAA_quanlity = 0;

	game_timer m_timer;

	ComPtr<IDXGIFactory4> m_dxgi_factory;
	ComPtr<IDXGISwapChain> m_swap_chain;
	ComPtr<ID3D12Device> m_d3d_device;

	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_current_fence = 0;

	ComPtr<ID3D12CommandQueue> m_command_queue;
	ComPtr<ID3D12CommandAllocator> m_direct_cmd_list_allocator;
	ComPtr<ID3D12GraphicsCommandList> m_command_list;

	static constexpr int SwapChainBufferCount = 2;
	int m_current_back_buffer = 0;
	ComPtr<ID3D12Resource> m_swap_chain_buffer[SwapChainBufferCount];
	ComPtr<ID3D12Resource> m_depth_stencil_buffer;

	ComPtr<ID3D12DescriptorHeap> m_rtv_heap;
	ComPtr<ID3D12DescriptorHeap> m_dsv_heap;

	D3D12_VIEWPORT m_screen_viewport;
	D3D12_RECT m_scissor_rect;

	UINT m_rtv_descriptor_size = 0;
	UINT m_dsv_descriptor_size = 0;
	UINT m_cbv_srv_uav_descriptor_size = 0;

	std::wstring m_main_window_caption = L"D3D Application";
	D3D_DRIVER_TYPE m_d3d_driver_type = D3D_DRIVER_TYPE_HARDWARE;
	DXGI_FORMAT m_back_buffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT m_depth_stencil_buffer_format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	int m_client_width = 800;
	int m_client_height = 600;

public:
	static d3d_application* application();

	HINSTANCE application_instance() const;
	HWND main_window() const;
	float aspect_ratio() const;

	bool get_4xMSAA_state() const;
	void set_4xMSAA_state(bool);

	int run();

	virtual bool initialize();
	virtual LRESULT message_process(HWND mwnd, UINT msg, WPARAM w_param, LPARAM l_param);
};
