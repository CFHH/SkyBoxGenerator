REM 把此文件放到grpc-1.38.x\cmake
set INSTALL_PATH=D:/grpc_sdk/win64_ue426
set UE_ROOT=F:\Program Files\Epic Games\UE_4.26
set CMAKE_BUILD_DIR=D:\grpc_sdk\grpc-1.38.x\cmake\build

IF EXIST "%CMAKE_BUILD_DIR%" (rmdir "%CMAKE_BUILD_DIR%" /s /q)
mkdir "%CMAKE_BUILD_DIR%" && cd "%CMAKE_BUILD_DIR%"

call cmake ../.. -G "Visual Studio 15 2017" -A x64 ^
    -Wno-dev -DCMAKE_INSTALL_PREFIX="%INSTALL_PATH%"  -DgRPC_INSTALL=ON ^
    -DCMAKE_CXX_STANDARD_LIBRARIES="Crypt32.Lib User32.lib Advapi32.lib" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_CONFIGURATION_TYPES=Release ^
    -Dprotobuf_BUILD_TESTS=OFF ^
    -DgRPC_ZLIB_PROVIDER=package ^
    -DZLIB_INCLUDE_DIR="%UE_ROOT%\Engine\Source\ThirdParty\zlib\v1.2.8\include\Win64\VS2015" ^
    -DZLIB_LIBRARY_DEBUG="%UE_ROOT%\Engine\Source\ThirdParty\zlib\v1.2.8\lib\Win64\VS2015\Debug\zlibstatic.lib" ^
    -DZLIB_LIBRARY_RELEASE="%UE_ROOT%\Engine\Source\ThirdParty\zlib\v1.2.8\lib\Win64\VS2015\Release\zlibstatic.lib" ^
    -DgRPC_SSL_PROVIDER=package ^
    -DLIB_EAY_LIBRARY_DEBUG="%UE_ROOT%\Engine\Source\ThirdParty\OpenSSL\1.1.1\Lib\Win64\VS2015\Debug\libcrypto.lib" ^
    -DLIB_EAY_LIBRARY_RELEASE="%UE_ROOT%\Engine\Source\ThirdParty\OpenSSL\1.1.1\Lib\Win64\VS2015\Release\libcrypto.lib" ^
    -DLIB_EAY_DEBUG="%UE_ROOT%\Engine\Source\ThirdParty\OpenSSL\1.1.1\Lib\Win64\VS2015\Debug\libcrypto.lib" ^
    -DLIB_EAY_RELEASE="%UE_ROOT%\Engine\Source\ThirdParty\OpenSSL\1.1.1\Lib\Win64\VS2015\Release\libcrypto.lib" ^
    -DOPENSSL_INCLUDE_DIR="%UE_ROOT%\Engine\Source\ThirdParty\OpenSSL\1.1.1\include\Win64\VS2015" ^
    -DSSL_EAY_DEBUG="%UE_ROOT%\Engine\Source\ThirdParty\OpenSSL\1.1.1\Lib\Win64\VS2015\Debug\libssl.lib" ^
    -DSSL_EAY_LIBRARY_DEBUG="%UE_ROOT%\Engine\Source\ThirdParty\OpenSSL\1.1.1\Lib\Win64\VS2015\Debug\libssl.lib" ^
    -DSSL_EAY_LIBRARY_RELEASE="%UE_ROOT%\Engine\Source\ThirdParty\OpenSSL\1.1.1\Lib\Win64\VS2015\Release\libssl.lib" ^
    -DSSL_EAY_RELEASE="%UE_ROOT%\Engine\Source\ThirdParty\OpenSSL\1.1.1\Lib\Win64\VS2015\Release\libssl.lib"
call cmake --build . --target ALL_BUILD --config Release
call cmake --install . --config Release