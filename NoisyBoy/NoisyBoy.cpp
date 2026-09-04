#define WIN32_LEAN_AND_MEAN
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "httplib.h" // NUESTRO NUEVO SERVIDOR
#include <iostream>
#include <filesystem>
#include <thread>     // NUEVO: Para dividir el programa en dos
#include <windows.h>
namespace fs = std::filesystem;
#pragma comment(lib, "ws2_32.lib")

struct DatosApp {
    ma_decoder decodificador;        // Para el Cable Virtual (Discord)
    ma_decoder decodificadorMonitor; // Para tus Auriculares
    bool reproduciendo = false;
};

// La bomba de agua de miniaudio (Queda igual)
void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    DatosApp* pDatos = (DatosApp*)pDevice->pUserData;
    float* salida = (float*)pOutput;
    const float* entradaMic = (const float*)pInput;

    if (entradaMic != NULL) {
        for (ma_uint32 i = 0; i < frameCount * pDevice->playback.channels; ++i) {
            salida[i] = entradaMic[i];
        }
    }

    if (pDatos->reproduciendo) {
        float bufferArchivo[8192];
        ma_uint64 framesLeidos = 0; 
        ma_decoder_read_pcm_frames(&pDatos->decodificador, bufferArchivo, frameCount, &framesLeidos);
        
        for (ma_uint32 i = 0; i < framesLeidos * pDevice->playback.channels; ++i) {
            salida[i] += bufferArchivo[i]; 
        }

        if (framesLeidos < frameCount) {
            pDatos->reproduciendo = false;
            ma_decoder_seek_to_pcm_frame(&pDatos->decodificador, 0); 
        }
    }
}

void data_callback_monitoreo(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    DatosApp* pDatos = (DatosApp*)pDevice->pUserData;
    float* salida = (float*)pOutput;

    if (pDatos->reproduciendo) {
        float bufferArchivo[8192];
        ma_uint64 framesLeidos = 0;
        ma_decoder_read_pcm_frames(&pDatos->decodificadorMonitor, bufferArchivo, frameCount, &framesLeidos);
        
        for (ma_uint32 i = 0; i < framesLeidos * pDevice->playback.channels; ++i) {
            salida[i] = bufferArchivo[i]; // Audio puro, sin tu voz
        }
    }
}

int main() {
    // 1. Inicializamos miniaudio (Mismo código de antes)
    ma_context context;
    ma_context_init(NULL, 0, NULL, &context);

    ma_device_info *pPlaybackInfos, *pCaptureInfos;
    ma_uint32 playbackCount, captureCount;
    ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount);

    DatosApp datos;
    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 2, 48000);
    ma_decoder_init_file("sonido.mp3", &decoderConfig, &datos.decodificador);
    ma_decoder_init_file("sonido.mp3", &decoderConfig, &datos.decodificadorMonitor); // <-- Agrega esta línea

    // --- 1. EL RADAR: Buscamos el Cable Virtual entre todos tus altavoces ---
    ma_device_id cablePlaybackID;
    bool cableEncontrado = false;

    for (ma_uint32 i = 0; i < playbackCount; ++i) {
        std::string nombreDispositivo = pPlaybackInfos[i].name;
        
        // Si el nombre contiene la palabra "CABLE Input", lo atrapamos
        if (nombreDispositivo.find("CABLE Input") != std::string::npos) {
            cablePlaybackID = pPlaybackInfos[i].id;
            cableEncontrado = true;
            std::cout << ">>> EXITO: VB-Cable detectado y conectado automaticamente (" << nombreDispositivo << ")" << std::endl;
            break; // Dejamos de buscar
        }
    }

    // --- 2. CONFIGURACIÓN DEL MOTOR ---
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_duplex);
    
    // El micrófono siempre usa el que tengas por defecto en Windows
    deviceConfig.capture.pDeviceID  = NULL; 
    deviceConfig.capture.format     = ma_format_f32;
    deviceConfig.capture.channels   = 2;
    
    // ¡La magia del Enrutamiento Inteligente!
    if (cableEncontrado) {
        // Si encontró el cable, manda el audio para Discord
        deviceConfig.playback.pDeviceID = &cablePlaybackID; 
    } else {
        // Si por alguna razón no lo encuentra, usa tus altavoces normales
        deviceConfig.playback.pDeviceID = NULL; 
        std::cout << ">>> ADVERTENCIA: No se encontro VB-Cable. Usando altavoces por defecto." << std::endl;
    }
    
    deviceConfig.playback.format    = ma_format_f32;
    deviceConfig.playback.channels  = 2;
    deviceConfig.sampleRate         = 48000;
    deviceConfig.dataCallback       = data_callback;
    deviceConfig.pUserData          = &datos;

    ma_device device;
    ma_device_init(&context, &deviceConfig, &device);
    ma_device_start(&device);

    // --- 3. EL SEGUNDO MOTOR (Tus Oídos) ---
    ma_device_config configMonitor = ma_device_config_init(ma_device_type_playback);
    configMonitor.playback.pDeviceID = NULL; // Esto fuerza a usar tus auriculares de siempre
    configMonitor.playback.format    = ma_format_f32;
    configMonitor.playback.channels  = 2;
    configMonitor.sampleRate         = 48000;
    configMonitor.dataCallback       = data_callback_monitoreo;
    configMonitor.pUserData          = &datos;

    ma_device deviceMonitor;
    ma_device_init(&context, &configMonitor, &deviceMonitor);
    ma_device_start(&deviceMonitor);
    
    // ==========================================
    // 2. EL NUEVO SERVIDOR WEB PARA EL FRONTEND
    // ==========================================
    httplib::Server svr;

    // Cuando la web pida la ruta "/play", ejecutamos esto:
    svr.Get("/play", [&](const httplib::Request& req, httplib::Response& res) {
        
        // 1. Verificamos si la web nos mandó el nombre de un archivo (ej. ?file=victoria.mp3)
        if (req.has_param("file")) {
            // Armamos la ruta completa: "sonidos/victoria.mp3"
            std::string archivoNuevo = "sonidos/" + req.get_param_value("file");
            
            // 2. Detenemos la reproducción actual
            datos.reproduciendo = false;
            
           // 3. "Desenchufamos" el archivo viejo de ambos decodificadores
        ma_decoder_uninit(&datos.decodificador);
        ma_decoder_uninit(&datos.decodificadorMonitor);
        
        // 4. "Enchufamos" el archivo nuevo en ambos a la vez
        if (ma_decoder_init_file(archivoNuevo.c_str(), NULL, &datos.decodificador) != MA_SUCCESS) {
            std::cout << "Error al cargar el archivo: " << archivoNuevo << std::endl;
            res.set_content("Error al cargar audio", "text/plain");
            return; // Salimos si hubo error
        }
        // Inicializamos el monitor (tus auriculares) sin necesidad de chequear error de nuevo
        ma_decoder_init_file(archivoNuevo.c_str(), NULL, &datos.decodificadorMonitor);
            datos.reproduciendo = true;
            
            std::cout << "> Orden recibida. Reproduciendo: " << req.get_param_value("file") << std::endl;
        } else {
             std::cout << "> Orden vacia recibida." << std::endl;
        }

        // Esto evita errores de seguridad en el navegador (CORS)
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content("Audio disparado", "text/plain");
    });

    std::cout << "--- NOISYBOY MOTOR Y SERVIDOR ACTIVOS ---" << std::endl;
    std::cout << "Tu voz esta cruzando hacia Discord." << std::endl;
    std::cout << "Servidor escuchando en http://localhost:8080" << std::endl;
    std::cout << "Haz doble clic en tu archivo 'index.html' para usar la botonera." << std::endl;
    std::cout << "-----------------------------------------" << std::endl;

    svr.Get("/api/sonidos", [](const httplib::Request& req, httplib::Response& res) {
        std::string json = "[";
        bool primero = true;
        for (const auto& entry : fs::directory_iterator("sonidos")) {
            if (entry.path().extension() == ".mp3" || entry.path().extension() == ".wav") {
                if (!primero) json += ",";
                json += "\"" + entry.path().filename().string() + "\"";
                primero = false;
            }
        }
        json += "]";
        
        // ¡Esta es la línea mágica que le da permiso al navegador!
        res.set_header("Access-Control-Allow-Origin", "*"); 
        
        res.set_content(json, "application/json");
    });
    // Esto reemplaza al "while(true)" del teclado. El servidor se queda escuchando para siempre.
    // 1. Hacemos que C++ ahora también sirva la interfaz visual, no solo los audios
    svr.set_mount_point("/", "./");

    // 2. Encendemos el servidor en un "Hilo" secundario (Background)
    std::thread hiloServidor([&svr]() {
        svr.listen("localhost", 8080);
    });

    // 3. Esperamos 1 segundo para asegurarnos de que el servidor esté 100% listo
    Sleep(1000);

    // 4. ¡Magia! Invocamos la ventana nativa. 
    // Usamos el motor de Edge porque está preinstalado en todo Windows 10/11.
    system("start msedge --app=\"http://localhost:8080/index.html\"");

    // 5. Le decimos al programa principal que se quede esperando a que el hilo termine
    hiloServidor.join();

    // Limpieza
    ma_device_uninit(&device);
    ma_decoder_uninit(&datos.decodificador);
    ma_context_uninit(&context);
    
    return 0;
}