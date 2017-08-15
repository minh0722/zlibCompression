//
// Game.h
//

#pragma once

#include "StepTimer.h"
#include "Decompressor.h"

// A basic game implementation that creates a D3D11 device and
// provides a game loop.
class Game
{
public:

    Game();

    // Initialization and management
    void Initialize(IUnknown* window);

    // Basic game loop
    void Tick();
    void Render();

    // Rendering helpers
    void Clear();
    void Present();

    // Messages
    void OnSuspending();
    void OnResuming();

private:

    void Update(DX::StepTimer const& timer);

    void CreateDevice();
    void CreateResources();

    void Decompress();

    // Application state
    IUnknown*                                       m_window;
    int                                             m_outputWidth;
    int                                             m_outputHeight;

    // Direct3D Objects
    D3D_FEATURE_LEVEL                               m_featureLevel;
    Microsoft::WRL::ComPtr<ID3D11DeviceX>           m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContextX>    m_d3dContext;

    // Rendering resources
    Microsoft::WRL::ComPtr<IDXGISwapChain1>         m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>  m_renderTargetView;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView>  m_depthStencilView;

    // Game state
    INT64                                           m_frame;
    DX::StepTimer                                   m_timer;

    // Decompress
    Decompressor                                    m_decompressor;
    std::thread                                     m_decompressTaskThread;
    ThreadGuard                                     m_decompressTaskThreadGuard;
};

// PIX event colors
const DWORD EVT_COLOR_FRAME = PIX_COLOR_INDEX(1);
const DWORD EVT_COLOR_UPDATE = PIX_COLOR_INDEX(2);
const DWORD EVT_COLOR_RENDER = PIX_COLOR_INDEX(3);
