#pragma once
#include "framework.h"
#include <d3d12.h>
#include <cstdlib>
#include <string>
#include <unordered_map>

#ifndef ThrowIfFailed
#define ThrowIfFailed(x)                                              \
{                                                                     \
    HRESULT hr__ = (x);                                               \
    std::wstring wfn = ansi_to_wstring(__FILE__);                       \
    if(FAILED(hr__)) { throw hresult_error(hr__, L#x, wfn, __LINE__); } \
}
#endif

#ifndef ReleaseCom
#define ReleaseCom(x) { if(x){ x->Release(); x = 0; } }
#endif

class hresult_error
{
public:
    hresult_error() = default;
    hresult_error(HRESULT, std::wstring, std::wstring, int);

    std::wstring what() const;

private:
    HRESULT m_error_code = S_OK;
    std::wstring m_function_name;
    std::wstring m_file_name;
    int m_line_number = -1;
};

inline std::wstring ansi_to_wstring(std::string const& str)
{
    WCHAR buffer[512];
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
    return std::wstring(buffer);
}

inline constexpr UINT calculate_constant_buffer_byte_size(UINT size)
{
    return (size + 255) & ~255;
}

ComPtr<ID3D12Resource> create_default_buffer(ID3D12Device*, ID3D12GraphicsCommandList*, void*, UINT64, ComPtr<ID3D12Resource>&);

ComPtr<ID3DBlob> compile_shader(std::wstring const&, D3D_SHADER_MACRO const*, std::string const&, std::string const&);

// Defines a subrange of geometry in a mesh_geometry.  This is for when multiple
// geometries are stored in one vertex and index buffer.  It provides the offsets
// and data needed to draw a subset of geometry stores in the vertex and index 
// buffers so that we can implement the technique described by Figure 6.3.
struct submesh_geometry
{
    UINT index_count = 0;
    UINT start_index_location = 0;
    INT base_vertex_location = 0;

    // Bounding box of the geometry defined by this submesh. 
    DirectX::BoundingBox bounds;
};

struct mesh_geometry
{
    // For look up by name
    std::string name;

    // System memory copies.  Use Blobs because the vertex/index format can be generic.
    // It is up to the client to cast appropriately.  
    ComPtr<ID3DBlob> vertex_buffer_cpu = nullptr;
    ComPtr<ID3DBlob> index_buffer_cpu = nullptr;

    ComPtr<ID3D12Resource> vertex_buffer_gpu = nullptr;
    ComPtr<ID3D12Resource> index_buffer_gpu = nullptr;
    ComPtr<ID3D12Resource> vertex_buffer_uploader = nullptr;
    ComPtr<ID3D12Resource> index_buffer_uploader = nullptr;

    // Data about the buffers.
    UINT vertex_byte_stride = 0;
    UINT vertex_buffer_byte_size = 0;
    DXGI_FORMAT index_format = DXGI_FORMAT_R16_UINT;
    UINT index_buffer_byte_size = 0;

    // A mesh_geometry may store multiple geometries in one vertex/index buffer.
    // Use this container to define the submesh geometries so we can draw
    // the submeshes individually.
    std::unordered_map<std::string, submesh_geometry> draw_args;

    inline D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view() const
    {
        D3D12_VERTEX_BUFFER_VIEW vbv{};
        vbv.BufferLocation = vertex_buffer_gpu->GetGPUVirtualAddress();
        vbv.StrideInBytes = vertex_byte_stride;
        vbv.SizeInBytes = vertex_buffer_byte_size;

        return vbv;
    }

    inline D3D12_INDEX_BUFFER_VIEW index_buffer_view() const
    {
        D3D12_INDEX_BUFFER_VIEW ibv{};
        ibv.BufferLocation = index_buffer_gpu->GetGPUVirtualAddress();
        ibv.Format = index_format;
        ibv.SizeInBytes = index_buffer_byte_size;

        return ibv;
    }

    void dispose_uploaders()
    {
        vertex_buffer_uploader = nullptr;
        index_buffer_uploader = nullptr;
    }
};

template<typename T>
class upload_buffer
{
private:
    ComPtr<ID3D12Resource> m_upload_buffer;
    BYTE* m_mapped_data;

    UINT m_element_byte_size = 0;
    bool m_is_constant_buffer = false;

public:
    upload_buffer(ID3D12Device* device, UINT element_count, bool is_constant_buffer) :
        m_element_byte_size(sizeof(T)), 
        m_is_constant_buffer(is_constant_buffer)
    {
        // Constant buffer elements need to be multiples of 256 bytes.
        if (m_is_constant_buffer)
        {
            m_element_byte_size = calculate_constant_buffer_byte_size(m_element_byte_size);
        }

        auto hp1 = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto rd1 = CD3DX12_RESOURCE_DESC::Buffer(m_element_byte_size * element_count);
        ThrowIfFailed(device->CreateCommittedResource(
            &hp1,
            D3D12_HEAP_FLAG_NONE,
            &rd1,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_upload_buffer)
        ));

        ThrowIfFailed(m_upload_buffer->Map(0, nullptr, reinterpret_cast<void**>(&m_mapped_data)));
    }

    upload_buffer(upload_buffer const&) = delete;
    upload_buffer& operator=(upload_buffer const&) = delete;

    ~upload_buffer()
    {
        if (m_upload_buffer != nullptr)
        {
            m_upload_buffer->Unmap(0, nullptr);
        }

        m_mapped_data = nullptr;
    }

    ID3D12Resource* resource() const
    {
        return m_upload_buffer.Get();
    }

    void copy_data(UINT element_index, T const& data)
    {
        std::memcpy(&m_mapped_data[element_index * m_element_byte_size], &data, sizeof(T));
    }
};
