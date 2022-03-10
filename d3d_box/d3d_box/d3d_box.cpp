#include "d3d_box.h"
#include <array>

d3d_box::d3d_box(HINSTANCE hinstance) : d3d_application(hinstance)
{}

d3d_box::~d3d_box() {}

bool d3d_box::initialize()
{
	if (!d3d_application::initialize())
	{
		return false;
	}

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(m_command_list->Reset(m_direct_cmd_list_allocator.Get(), nullptr));

	build_descriptor_heaps();
	build_constant_buffer();
	build_root_signature();
	build_shader_and_input_layout();
	build_box_geometry();
	build_pso();

	// Execute the initialization commands.
	ThrowIfFailed(m_command_list->Close());
	ID3D12CommandList* cmds_lists[]{ m_command_list.Get() };
	m_command_queue->ExecuteCommandLists(_countof(cmds_lists), cmds_lists);

	// Wait until initialization is complete.
	flush_command_queue();

	return true;
}

void d3d_box::on_resize()
{
	d3d_application::on_resize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	auto p = DirectX::XMMatrixPerspectiveFovLH(0.25f * math_helper::pi, aspect_ratio(), 1.0f, 1000.0f);
	DirectX::XMStoreFloat4x4(&m_projection, p);
}

void d3d_box::update(game_timer const& timer)
{
	// Convert Spherical to Cartesian coordinates.
	auto x = m_radious * std::sinf(m_phi) * std::cosf(m_theta);
	auto z = m_radious * std::sinf(m_phi) * std::sinf(m_theta);
	auto y = m_radious * std::cosf(m_phi);

	// Build the view matrix.
	auto position = DirectX::XMVectorSet(x, y, z, 1.0f);
	auto target = DirectX::XMVectorZero();
	auto up = DirectX::XMVectorSet(.0f, 1.0f, .0f, .0f);

	auto view = DirectX::XMMatrixLookAtLH(position, target, up);
	DirectX::XMStoreFloat4x4(&m_view, view);

	auto world = DirectX::XMLoadFloat4x4(&m_world);
	auto projection = DirectX::XMLoadFloat4x4(&m_projection);
	auto world_view_projection = world * view * projection;

	// Update the constant buffer with the latest worldViewProj matrix.
	object_constant obj_constant{};
	DirectX::XMStoreFloat4x4(&obj_constant.world_view_projection,
		DirectX::XMMatrixTranspose(world_view_projection));
	m_object_constant_buffer->copy_data(0, obj_constant);
}

void d3d_box::draw(game_timer const& timer)
{
	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	//flush_command_queue();
	ThrowIfFailed(m_direct_cmd_list_allocator->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(m_command_list->Reset(m_direct_cmd_list_allocator.Get(), m_pso.Get()));

	m_command_list->RSSetViewports(1, &m_screen_viewport);
	m_command_list->RSSetScissorRects(1, &m_scissor_rect);

	// Indicate a state transition on the resource usage.
	auto const& rb1 = CD3DX12_RESOURCE_BARRIER::Transition(current_back_buffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_command_list->ResourceBarrier(1, &rb1);

	// Clear the back buffer and depth buffer.
	m_command_list->ClearRenderTargetView(current_back_buffer_view(), DirectX::Colors::LightSteelBlue, 0, nullptr);
	m_command_list->ClearDepthStencilView(depth_stencil_view(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	auto const& bbv = current_back_buffer_view();
	auto const& dsv = depth_stencil_view();
	m_command_list->OMSetRenderTargets(1, &bbv, true, &dsv);

	ID3D12DescriptorHeap* descriptor_heaps[]{ m_cbv_heap.Get() };
	m_command_list->SetDescriptorHeaps(_countof(descriptor_heaps), descriptor_heaps);

	m_command_list->SetGraphicsRootSignature(m_root_signature.Get());

	auto const& vbv = m_box_geometry->vertex_buffer_view();
	auto const& ibv = m_box_geometry->index_buffer_view();
	m_command_list->IASetVertexBuffers(0, 1, &vbv);
	m_command_list->IASetIndexBuffer(&ibv);
	m_command_list->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	m_command_list->SetGraphicsRootDescriptorTable(0, m_cbv_heap->GetGPUDescriptorHandleForHeapStart());

	m_command_list->DrawIndexedInstanced(m_box_geometry->draw_args["box"].index_count, 1, 0, 0, 0);

	// Indicate a state transition on the resource usage.
	auto const& rb2 = CD3DX12_RESOURCE_BARRIER::Transition(current_back_buffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_command_list->ResourceBarrier(1, &rb2);

	// Done recording commands.
	ThrowIfFailed(m_command_list->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmds_lists[]{ m_command_list.Get() };
	m_command_queue->ExecuteCommandLists(_countof(cmds_lists), cmds_lists);

	// swap the back and front buffers
	ThrowIfFailed(m_swap_chain->Present(0, 0));
	m_current_back_buffer = (m_current_back_buffer + 1) % SwapChainBufferCount;

	// Wait until frame commands are complete.
	flush_command_queue();
}

void d3d_box::on_mouse_down(WPARAM wparam, int x, int y)
{
	m_last_mouse_position.x = x;
	m_last_mouse_position.y = y;

	SetCapture(m_main_window);
}

void d3d_box::on_mouse_up(WPARAM, int, int)
{
	ReleaseCapture();
}

void d3d_box::on_mouse_move(WPARAM button_state, int x, int y)
{
	if ((button_state & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		auto dx = DirectX::XMConvertToRadians(0.25f * static_cast<float>(x - m_last_mouse_position.x));
		auto dy = DirectX::XMConvertToRadians(0.25f * static_cast<float>(y - m_last_mouse_position.y));

		// Update angles based on input to orbit camera around box.
		m_theta -= dx;
		m_phi -= dy;

		// Restrict the angle mPhi.
		m_phi = math_helper::clamp(m_phi, 0.1f, math_helper::pi - 0.1f);
	}
	else if ((button_state & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.005 unit in the scene.
		auto dx = 0.005f * static_cast<float>(x - m_last_mouse_position.x);
		auto dy = 0.005f * static_cast<float>(y - m_last_mouse_position.y);

		// Update the camera radius based on input.
		m_radious += dy - dx;

		// Restrict the radius.
		m_radious = math_helper::clamp(m_radious, 3.0f, 15.0f);
	}

	m_last_mouse_position.x = x;
	m_last_mouse_position.y = y;
}

void d3d_box::build_descriptor_heaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{};
	cbvHeapDesc.NumDescriptors = 1;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(m_d3d_device->CreateDescriptorHeap(&cbvHeapDesc,
		IID_PPV_ARGS(&m_cbv_heap)));
}

void d3d_box::build_constant_buffer()
{
	m_object_constant_buffer = std::make_unique<upload_buffer<object_constant>>(m_d3d_device.Get(), 1, true);

	UINT constexpr objCBByteSize = calculate_constant_buffer_byte_size(sizeof(object_constant));

	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_object_constant_buffer->resource()->GetGPUVirtualAddress();
	// Offset to the ith object constant buffer in the buffer.
	int boxCBufIndex = 0;
	cbAddress += boxCBufIndex * objCBByteSize;

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
	cbvDesc.BufferLocation = cbAddress;
	cbvDesc.SizeInBytes = calculate_constant_buffer_byte_size(sizeof(object_constant));

	m_d3d_device->CreateConstantBufferView(
		&cbvDesc,
		m_cbv_heap->GetCPUDescriptorHandleForHeapStart());
}

void d3d_box::build_root_signature()
{
	// Shader programs typically require resources as input (constant buffers,
	// textures, samplers).  The root signature defines the resources the shader
	// programs expect.  If we think of the shader programs as a function, and
	// the input resources as function parameters, then the root signature can be
	// thought of as defining the function signature.  

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[1]{};

	// Create a single descriptor table of CBVs.
	CD3DX12_DESCRIPTOR_RANGE cbvTable{};
	cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameter, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		&serializedRootSig, &errorBlob);

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(m_d3d_device->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&m_root_signature)));
}

void d3d_box::build_shader_and_input_layout()
{
	m_vs_bytecode = compile_shader(L"shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
	m_ps_bytecode = compile_shader(L"shaders\\color.hlsl", nullptr, "PS", "ps_5_0");

	m_input_layout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA}
	};
}

void d3d_box::build_box_geometry()
{
	std::array vertices =
	{
		vertex({ DirectX::XMFLOAT3(-1.0f, -1.0f, -1.0f), DirectX::XMFLOAT4(DirectX::Colors::White) }),
		vertex({ DirectX::XMFLOAT3(-1.0f, +1.0f, -1.0f), DirectX::XMFLOAT4(DirectX::Colors::Black) }),
		vertex({ DirectX::XMFLOAT3(+1.0f, +1.0f, -1.0f), DirectX::XMFLOAT4(DirectX::Colors::Red) }),
		vertex({ DirectX::XMFLOAT3(+1.0f, -1.0f, -1.0f), DirectX::XMFLOAT4(DirectX::Colors::Green) }),
		vertex({ DirectX::XMFLOAT3(-1.0f, -1.0f, +1.0f), DirectX::XMFLOAT4(DirectX::Colors::Blue) }),
		vertex({ DirectX::XMFLOAT3(-1.0f, +1.0f, +1.0f), DirectX::XMFLOAT4(DirectX::Colors::Yellow) }),
		vertex({ DirectX::XMFLOAT3(+1.0f, +1.0f, +1.0f), DirectX::XMFLOAT4(DirectX::Colors::Cyan) }),
		vertex({ DirectX::XMFLOAT3(+1.0f, -1.0f, +1.0f), DirectX::XMFLOAT4(DirectX::Colors::Magenta) })
	};

	std::array<std::uint16_t, 36> indicies =
	{
		// front face
		0, 1, 2,
		0, 2, 3,

		// back face
		4, 6, 5,
		4, 7, 6,

		// left face
		4, 5, 1,
		4, 1, 0,

		// right face
		3, 2, 6,
		3, 6, 7,

		// top face
		1, 5, 6,
		1, 6, 2,

		// bottom face
		4, 0, 3,
		4, 3, 7
	};

	UINT const vb_byte_size = vertices.size() * sizeof(vertex);
	UINT const ib_byte_size = indicies.size() * sizeof(std::uint16_t);

	m_box_geometry = std::make_unique<mesh_geometry>();
	m_box_geometry->name = "boxGeo";

	ThrowIfFailed(D3DCreateBlob(vb_byte_size, &m_box_geometry->vertex_buffer_cpu));
	CopyMemory(m_box_geometry->vertex_buffer_cpu->GetBufferPointer(), vertices.data(), vb_byte_size);

	ThrowIfFailed(D3DCreateBlob(ib_byte_size, &m_box_geometry->index_buffer_cpu));
	CopyMemory(m_box_geometry->index_buffer_cpu->GetBufferPointer(), indicies.data(), ib_byte_size);

	m_box_geometry->vertex_buffer_gpu = create_default_buffer(m_d3d_device.Get(),
		m_command_list.Get(), vertices.data(), vb_byte_size, m_box_geometry->vertex_buffer_uploader);

	m_box_geometry->index_buffer_gpu = create_default_buffer(m_d3d_device.Get(),
		m_command_list.Get(), indicies.data(), ib_byte_size, m_box_geometry->index_buffer_uploader);

	m_box_geometry->vertex_byte_stride = sizeof(vertex);
	m_box_geometry->vertex_buffer_byte_size = vb_byte_size;
	m_box_geometry->index_format = DXGI_FORMAT_R16_UINT;
	m_box_geometry->index_buffer_byte_size = ib_byte_size;

	submesh_geometry submesh{};
	submesh.index_count = (UINT)indicies.size();
	submesh.start_index_location = 0;
	submesh.base_vertex_location = 0;

	m_box_geometry->draw_args["box"] = submesh;
}

void d3d_box::build_pso()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout.pInputElementDescs = m_input_layout.data();
	psoDesc.InputLayout.NumElements = (UINT)m_input_layout.size();
	//psoDesc.InputLayout = { m_input_layout.data(), (UINT)m_input_layout.size() };
	psoDesc.pRootSignature = m_root_signature.Get();
	psoDesc.VS.pShaderBytecode = reinterpret_cast<BYTE*>(m_vs_bytecode->GetBufferPointer());
	psoDesc.VS.BytecodeLength = m_vs_bytecode->GetBufferSize();
	psoDesc.PS.pShaderBytecode = reinterpret_cast<BYTE*>(m_ps_bytecode->GetBufferPointer());
	psoDesc.PS.BytecodeLength = m_ps_bytecode->GetBufferSize();
	/*psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(m_vs_bytecode->GetBufferPointer()),
		m_vs_bytecode->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(m_ps_bytecode->GetBufferPointer()),
		m_ps_bytecode->GetBufferSize()
	};*/
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = m_back_buffer_format;
	psoDesc.SampleDesc.Count = m_4xMSAA_enable ? 4 : 1;
	psoDesc.SampleDesc.Quality = m_4xMSAA_enable ? (m_4xMSAA_quanlity - 1) : 0;
	psoDesc.DSVFormat = m_depth_stencil_buffer_format;
	ThrowIfFailed(m_d3d_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso)));
}
