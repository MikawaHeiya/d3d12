#include "d3d_utility.h"
#include <comdef.h>

hresult_error::hresult_error(HRESULT code, std::wstring func_name, std::wstring file_name, int line_num) : 
	m_error_code(code), m_function_name(std::move(func_name)), 
	m_file_name(std::move(file_name)), m_line_number(line_num)
{
}

std::wstring hresult_error::what() const
{
	_com_error err(m_error_code);
	std::wstring msg = err.ErrorMessage();

	return m_function_name + L" failed in " + m_file_name + L"; line " + std::to_wstring(m_line_number) + L"; error: " + msg;
}

ComPtr<ID3D12Resource> create_default_buffer(ID3D12Device* device, ID3D12GraphicsCommandList* command_list, void* init_data, UINT64 byte_size, ComPtr<ID3D12Resource>& upload_buffer)
{
	ComPtr<ID3D12Resource> defaultBuffer;

	// Create the actual default buffer resource.
	auto hp1 = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto rd1 = CD3DX12_RESOURCE_DESC::Buffer(byte_size);
	ThrowIfFailed(device->CreateCommittedResource(
		&hp1,
		D3D12_HEAP_FLAG_NONE,
		&rd1,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

	// In order to copy CPU memory data into our default buffer, we need to create
	// an intermediate upload heap. 
	auto hp2 = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto rd2 = CD3DX12_RESOURCE_DESC::Buffer(byte_size);
	ThrowIfFailed(device->CreateCommittedResource(
		&hp2,
		D3D12_HEAP_FLAG_NONE,
		&rd2,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(upload_buffer.GetAddressOf())));


	// Describe the data we want to copy into the default buffer.
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = init_data;
	subResourceData.RowPitch = byte_size;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	// Schedule to copy the data to the default buffer resource.  At a high level, the helper function UpdateSubresources
	// will copy the CPU memory into the intermediate upload heap.  Then, using ID3D12CommandList::CopySubresourceRegion,
	// the intermediate upload heap data will be copied to mBuffer.
	auto rb1 = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	command_list->ResourceBarrier(1, &rb1);
	UpdateSubresources<1>(command_list, defaultBuffer.Get(), upload_buffer.Get(), 0, 0, 1, &subResourceData);
	auto rb2 = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	command_list->ResourceBarrier(1, &rb2);

	// Note: uploadBuffer has to be kept alive after the above function calls because
	// the command list has not been executed yet that performs the actual copy.
	// The caller can Release the uploadBuffer after it knows the copy has been executed.

	return defaultBuffer;
}

ComPtr<ID3DBlob> compile_shader(
	std::wstring const& filename, 
	D3D_SHADER_MACRO const* defines, 
	std::string const& entrypoint, 
	std::string const& target)
{
	UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = S_OK;

	ComPtr<ID3DBlob> byteCode = nullptr;
	ComPtr<ID3DBlob> errors;
	hr = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

	if (errors != nullptr)
		OutputDebugStringA((char*)errors->GetBufferPointer());

	ThrowIfFailed(hr);

	return byteCode;
}
