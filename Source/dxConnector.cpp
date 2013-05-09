/**
	Copyright 2013 Elio <elio@r-revue.de>
	
	
	This file is part of RR-DXBridge

    RR-DXBridge is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    RR-DXBridge is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with RR-DXBridge.  If not, see <http://www.gnu.org/licenses/>.

    Diese Datei ist Teil von RR-DXBridge.

    RR-DXBridge ist Freie Software: Sie können es unter den Bedingungen
    der GNU Lesser General Public License, wie von der Free Software Foundation,
    Version 3 der Lizenz oder (nach Ihrer Option) jeder späteren
    veröffentlichten Version, weiterverbreiten und/oder modifizieren.

    RR-DXBridge wird in der Hoffnung, dass es nützlich sein wird, aber
    OHNE JEDE GEWÄHELEISTUNG, bereitgestellt; sogar ohne die implizite
    Gewährleistung der MARKTFÄHIGKEIT oder EIGNUNG FÜR EINEN BESTIMMTEN ZWECK.
    Siehe die GNU Lesser General Public License für weitere Details.

    Sie sollten eine Kopie der GNU Lesser General Public License zusammen mit diesem
    Programm erhalten haben. Wenn nicht, siehe <http://www.gnu.org/licenses/>.
 */

#include "dxconnector.h"
#include <string>

PFNWGLDXOPENDEVICENVPROC wglDXOpenDeviceNV = NULL;
PFNWGLDXREGISTEROBJECTNVPROC wglDXRegisterObjectNV = NULL;
PFNWGLDXSETRESOURCESHAREHANDLENVPROC wglDXSetResourceShareHandleNV = NULL;
PFNWGLDXLOCKOBJECTSNVPROC wglDXLockObjectsNV = NULL;
PFNWGLDXUNLOCKOBJECTSNVPROC wglDXUnlockObjectsNV = NULL;
PFNWGLDXCLOSEDEVICENVPROC wglDXCloseDeviceNV = NULL;
PFNWGLDXUNREGISTEROBJECTNVPROC wglDXUnregisterObjectNV = NULL;

DXGLConnector::DXGLConnector() {
	m_pD3D = NULL;
	m_pDevice = NULL;
	m_dxTexture = NULL;
	m_glTextureHandle = NULL;
	m_glTextureName = 0;
	m_hWnd = NULL;
	m_bInitialized = FALSE;
	strcpy_s( m_shardMemoryName, "" );
	getNvExt();
}

DXGLConnector::~DXGLConnector() {
	m_bInitialized = FALSE;
}

// this function initializes and prepares Direct3D
BOOL DXGLConnector::init(HWND hWnd, BOOL bReceive)
{
	HRESULT res;
    res = Direct3DCreate9Ex(D3D_SDK_VERSION, &m_pD3D);
	if ( res != D3D_OK ) {
		MessageBox(m_hWnd, "Failed to initialize DX9EX", "Error", 0);
		return FALSE;
	}

    D3DPRESENT_PARAMETERS d3dpp;

    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = hWnd;
		// this seems to be quite a dummy thing because we use directx only for accessing the handle
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
		// some dummy resolution - we don't render anything
    d3dpp.BackBufferWidth = 1920;
    d3dpp.BackBufferHeight = 1080;
	
	// attention: changed this from device9ex to device9 (perhaps it needs to be changed back, but i want to have same code as in dx_interop sample code)

    // create a device class using this information and the info from the d3dpp stuct
    res = m_pD3D->CreateDeviceEx(D3DADAPTER_DEFAULT,
                      D3DDEVTYPE_HAL,
                      m_hWnd,
                      D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE | D3DCREATE_MULTITHREADED,
                      &d3dpp,
					  NULL,
                      &m_pDevice);
	if ( res != D3D_OK ) {
		MessageBox(m_hWnd, "Failed to create DX9EX Device", "Error", 0);
		return FALSE;
	}
		
	m_InteropHandle = wglDXOpenDeviceNV(m_pDevice);
	if ( m_InteropHandle == NULL ) {
		MessageBox(m_hWnd, "NVidia wglDXInterop failed to initialize.", "Error", 0);
		return FALSE;
	}
		// prepare gl texture
	glGenTextures(1, &m_glTextureName);

		// directly connect to texture if possible
	connectToTexture(bReceive);
	m_bInitialized = TRUE;

	return TRUE;
}

// this is the function that cleans up Direct3D and COM
void DXGLConnector::cleanup()
{
	if (m_glTextureName) {
		glDeleteTextures(1, &m_glTextureName);
		m_glTextureName = 0;
	}
	if ( m_glTextureHandle != NULL ) { // already a texture connected => unregister interop
		wglDXUnregisterObjectNV(m_InteropHandle, m_glTextureHandle);
		m_glTextureHandle = NULL;
	}

	if ( m_InteropHandle != NULL ) {
		wglDXCloseDeviceNV(m_InteropHandle);
	}

    if ( m_pDevice != NULL ) {
		m_pDevice->Release();    // close and release the 3D device
	}
	if ( m_pD3D != NULL ) {
	    m_pD3D->Release();    // close and release Direct3D
	}

	m_bInitialized = FALSE;
}

void DXGLConnector::setSharedMemoryName(char* sharedMemoryName) {
	if ( strcmp(sharedMemoryName, m_shardMemoryName) == 0 ) {
		return;
	}
	strcpy_s( m_shardMemoryName, sharedMemoryName );
	if ( m_bInitialized ) { // directly connect to texture (if already initialized)
		connectToTexture();
	}
}

BOOL DXGLConnector::Reload() {
	if ( m_bInitialized ) { // directly connect to texture (if already initialized)
		return connectToTexture();
	}
	return FALSE;
}

/**
 * connectToTexture()
 * 
 * bReceive		when receiving a texture from a DX application this must be set to TRUE (default)
 *				when sending a texture from GL to the DX application, set to FALSE
 */
BOOL DXGLConnector::connectToTexture( BOOL bReceive ) {
	if ( m_glTextureHandle != NULL ) { // already a texture connected => unregister interop
		wglDXUnregisterObjectNV(m_InteropHandle, m_glTextureHandle);
		m_glTextureHandle = NULL;
	}

	if ( bReceive && !getSharedTextureInfo(m_shardMemoryName) ) {  // error accessing shared memory texture info
		return FALSE;
	}

	if ( bReceive ) {
		if ( m_TextureInfo.shareHandle == NULL ) {
			return FALSE;
		}
	} else {
			// create a new shared resource
		m_TextureInfo.shareHandle = NULL;
	}
	LPDIRECT3DTEXTURE9* ppTexture = bReceive ? &m_dxTexture : NULL;
	HRESULT res = m_pDevice->CreateTexture(m_TextureInfo.width,m_TextureInfo.height,1,D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_dxTexture, &m_TextureInfo.shareHandle );
		// USAGE may also be D3DUSAGE_DYNAMIC and pay attention to format and resolution!!!
	if ( res != D3D_OK ) {
		return FALSE;
	}

		// prepare shared resource
	if (!wglDXSetResourceShareHandleNV(m_dxTexture, m_TextureInfo.shareHandle) ) {
			// this is not only a non-accessible share-handle, something worse
		MessageBox(m_hWnd, "wglDXSetResourceShareHandleNV() failed.", "Error", 0);
		return FALSE;
	}

		// register for interop and associate with dx texture
	m_glTextureHandle = wglDXRegisterObjectNV(m_InteropHandle, m_dxTexture,
		m_glTextureName,
		GL_TEXTURE_2D,
		WGL_ACCESS_READ_WRITE_NV);

	return TRUE;
}

/**
* Load the Nvidia-Extensions dynamically
*/
BOOL DXGLConnector::getNvExt() 
{
	wglDXOpenDeviceNV = (PFNWGLDXOPENDEVICENVPROC)wglGetProcAddress("wglDXOpenDeviceNV");
	if(wglDXOpenDeviceNV == NULL)
	{
		MessageBox(m_hWnd, "wglDXOpenDeviceNV ext is not supported by your GPU.", "Error", 0);
		return FALSE;
	}
	wglDXRegisterObjectNV = (PFNWGLDXREGISTEROBJECTNVPROC)wglGetProcAddress("wglDXRegisterObjectNV");
	if(wglDXRegisterObjectNV == NULL)
	{
		MessageBox(m_hWnd, "wglDXRegisterObjectNV ext is not supported by your GPU.", "Error", 0);
		return FALSE;
	}
	wglDXUnregisterObjectNV = (PFNWGLDXUNREGISTEROBJECTNVPROC)wglGetProcAddress("wglDXUnregisterObjectNV");
	if(wglDXUnregisterObjectNV == NULL)
	{
		MessageBox(m_hWnd, "wglDXRegisterObjectNV ext is not supported by your GPU.", "Error", 0);
		return FALSE;
	}
	wglDXSetResourceShareHandleNV = (PFNWGLDXSETRESOURCESHAREHANDLENVPROC)wglGetProcAddress("wglDXSetResourceShareHandleNV");
	if(wglDXSetResourceShareHandleNV == NULL)
	{
		MessageBox(m_hWnd, "wglDXSetResourceShareHandleNV ext is not supported by your GPU.", "Error", 0);
		return FALSE;
	}
	wglDXLockObjectsNV = (PFNWGLDXLOCKOBJECTSNVPROC)wglGetProcAddress("wglDXLockObjectsNV");
	if(wglDXLockObjectsNV == NULL)
	{
		MessageBox(m_hWnd, "wglDXLockObjectsNV ext is not supported by your GPU.", "Error", 0);
		return FALSE;
	}
	wglDXUnlockObjectsNV = (PFNWGLDXUNLOCKOBJECTSNVPROC)wglGetProcAddress("wglDXUnlockObjectsNV");
	if(wglDXUnlockObjectsNV == NULL)
	{
		MessageBox(m_hWnd, "wglDXUnlockObjectsNV ext is not supported by your GPU.", "Error", 0);
		return FALSE;
	}
	wglDXCloseDeviceNV = (PFNWGLDXCLOSEDEVICENVPROC)wglGetProcAddress("wglDXCloseDeviceNV");
	if(wglDXUnlockObjectsNV == NULL)
	{
		MessageBox(m_hWnd, "wglDXCloseDeviceNV ext is not supported by your GPU.", "Error", 0);
		return FALSE;
	}

	return TRUE;
}

BOOL DXGLConnector::getSharedTextureInfo(char* sharedMemoryName) {
	HANDLE hMapFile;
	LPCTSTR pBuf;

	hMapFile = OpenFileMapping(
					FILE_MAP_READ,   // read/write access
					FALSE,                 // do not inherit the name
					sharedMemoryName);               // name of mapping object

	if (hMapFile == NULL) {
		// no texture to share
		return FALSE;
	}

	pBuf = (LPTSTR) MapViewOfFile(hMapFile, // handle to map object
				FILE_MAP_READ,  // read/write permission
				0,
				0,
				sizeof(DX9SharedTextureInfo) );

	if (pBuf == NULL)
	{
		MessageBox(NULL, "Could not map view of file.", "Error", 0), 
		CloseHandle(hMapFile);
		return FALSE;
	}

	memcpy( &m_TextureInfo, pBuf, sizeof(DX9SharedTextureInfo) );
	
	UnmapViewOfFile(pBuf);
	CloseHandle(hMapFile);

	return TRUE;
}