#ifndef UNICODE
#define UNICODE
#endif

#include "application_windows.hpp"
#include "http/listener.hpp"

#include <loguru.hpp>
#include <shellapi.h>

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

dasa::gliese::scanner::windows::Application *windows_application;
dasa::gliese::scanner::Application *application;

using dasa::gliese::scanner::windows::Application;

void Application::initialize(std::shared_ptr<dasa::gliese::scanner::http::Listener> listener) {
    LOG_SCOPE_FUNCTION(INFO);
    LOG_S(INFO) << "Initializing Windows application";
    dasa::gliese::scanner::Application::initialize(listener);

    LOG_S(INFO) << "Loading TWAIN DSM library";
    twain.loadDSM("TWAINDSM.dll");

    LOG_S(INFO) << "Creating main window";
    
    const wchar_t CLASS_NAME[] = L"Gliese Scanner";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    hwnd = CreateWindowEx(0, CLASS_NAME, L"Gliese Scanner", 0, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr, nullptr, GetModuleHandle(NULL), nullptr);

    if (!hwnd) {
        ABORT_S() << "Failed to create main window";
        return;
    }

    ShowWindow(hwnd, SW_HIDE);
}

void Application::run() {
    LOG_SCOPE_FUNCTION(INFO);
    LOG_S(INFO) << "Application running";

    listener->start();

    std::thread ioThread([&] { ioc.run(); });

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    ioc.stop();
}

/*int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
	int argc;
	auto wargv = CommandLineToArgvW(pCmdLine, &argc);
	char** argv = new char* [argc + 1];
	for (int i = 0; i < argc; ++i) {
		auto bufSize = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], 0, nullptr, nullptr);
		argv[i] = new char[bufSize + 1];
		argv[i][bufSize] = '\0';
		WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], bufSize, nullptr, nullptr);
	}
    argv[argc] = nullptr;*/
int main(int argc, char **argv) {
    loguru::init(argc, argv, "-v");

	/*for (int i = 0; i < argc; ++i) {
		delete[] argv[i];
	}
	delete[] argv;*/

	windows_application = new Application;
	application = windows_application;
	auto listener = std::make_shared<dasa::gliese::scanner::http::Listener>(application->getIoContext());

	listener->listen("127.0.0.1", 43456);

	LOG_F(INFO, "Opening HTTP listener on 127.0.0.1:43456");

	application->initialize(listener);

	LOG_F(INFO, "Application initialized");

	LOG_S(INFO) << "Connecting to TWAIN";
	application->getTwain().fillIdentity();
	application->getTwain().openDSM();

	LOG_S(INFO) << "Listing TWAIN devices";
	auto devices = application->getTwain().listSources();
	for (auto& device : devices) {
		LOG_S(INFO) << device;
	}

	auto device = application->getTwain().getDefaultDataSource();
	LOG_S(INFO) << "Default device: " << device;

	application->run();

	LOG_F(INFO, "Closing TWAIN DSM");
	application->getTwain().closeDSM();
	return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
