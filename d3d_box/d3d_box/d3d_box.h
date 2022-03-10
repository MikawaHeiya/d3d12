#pragma once
#include "../../d3d_application/d3d_application/d3d_application.h"
#include "math_helper.h"
#include <memory>

struct vertex
{
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT4 color;
};

struct object_constant
{
	DirectX::XMFLOAT4X4 world_view_projection = math_helper::identity4x4;
};

class d3d_box : public d3d_application
{
public:
	d3d_box(HINSTANCE);
	d3d_box(d3d_box const&) = delete;
	d3d_box& operator=(d3d_box const&) = delete;
	~d3d_box();

	virtual bool initialize() override;

private:
	virtual void on_resize() override;
	virtual void update(game_timer const&) override;
	virtual void draw(game_timer const&) override;

	virtual void on_mouse_down(WPARAM, int, int) override;
	virtual void on_mouse_up(WPARAM, int, int) override;
	virtual void on_mouse_move(WPARAM, int, int) override;

	void build_descriptor_heaps();
	void build_constant_buffer();
	void build_root_signature();
	void build_shader_and_input_layout();
	void build_box_geometry();
	void build_pso();

	ComPtr<ID3D12RootSignature> m_root_signature = nullptr;
	ComPtr<ID3D12DescriptorHeap> m_cbv_heap = nullptr;

	std::unique_ptr<upload_buffer<object_constant>> m_object_constant_buffer = nullptr;
	std::unique_ptr<mesh_geometry> m_box_geometry = nullptr;

	ComPtr<ID3DBlob> m_vs_bytecode = nullptr;
	ComPtr<ID3DBlob> m_ps_bytecode = nullptr;

	std::vector<D3D12_INPUT_ELEMENT_DESC> m_input_layout;

	ComPtr<ID3D12PipelineState> m_pso = nullptr;

	DirectX::XMFLOAT4X4 m_world = math_helper::identity4x4;
	DirectX::XMFLOAT4X4 m_view = math_helper::identity4x4;
	DirectX::XMFLOAT4X4 m_projection = math_helper::identity4x4;

	float m_theta = 1.5f * DirectX::XM_PI;
	float m_phi = DirectX::XM_PIDIV4;
	float m_radious = 5.0f;

	POINT m_last_mouse_position;
};

