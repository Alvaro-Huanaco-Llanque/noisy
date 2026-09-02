#define WIN32_LEAN_AND_MEAN
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "httplib.h" // NUESTRO NUEVO SERVIDOR
#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;
#pragma comment(lib, "ws2_32.lib")

struct DatosApp {
    ma_decoder decodificador;
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

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_duplex);
    deviceConfig.capture.pDeviceID  = NULL;
    deviceConfig.capture.format     = ma_format_f32;
    deviceConfig.capture.channels   = 2;
    deviceConfig.playback.pDeviceID = NULL;
    deviceConfig.playback.format    = ma_format_f32;
    deviceConfig.playback.channels  = 2;
    deviceConfig.sampleRate         = 48000;
    deviceConfig.dataCallback       = data_callback;
    deviceConfig.pUserData          = &datos; 

    ma_device device;
    ma_device_init(&context, &deviceConfig, &device);
    ma_device_start(&device);
    
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
            
            // 3. "Desenchufamos" el archivo viejo de miniaudio
            ma_decoder_uninit(&datos.decodificador);
            
            // 4. "Enchufamos" el archivo nuevo
            if (ma_decoder_init_file(archivoNuevo.c_str(), NULL, &datos.decodificador) != MA_SUCCESS) {
                std::cout << "Error al cargar el archivo: " << archivoNuevo << std::endl;
                res.set_content("Error al cargar audio", "text/plain");
                return; // Salimos si hubo error
            }
            
            // 5. ¡Le decimos a miniaudio que dispare el nuevo sonido!
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
    res.set_content(json, "application/json");
});
    // Esto reemplaza al "while(true)" del teclado. El servidor se queda escuchando para siempre.
    svr.listen("localhost", 8080);

    // Limpieza
    ma_device_uninit(&device);
    ma_decoder_uninit(&datos.decodificador);
    ma_context_uninit(&context);
    
    return 0;
}