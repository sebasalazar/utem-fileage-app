/**
 * @file
 * @brief Pipeline productor–consumidor con OpenMP, cola lock-free (MPMC) y mapa concurrente para histogramar edades.
 *
 * @details
 * ### Propósito
 * Este ejecutable implementa un pipeline concurrente orientado a throughput:
 * - **Productor único** (OpenMP `single nowait`) que lee un archivo texto/CSV línea a línea y encola punteros a `std::string`
 *   en una estructura lock-free **MPMC** (`boost::lockfree::queue`).
 * - **Consumidores** (todos los hilos de la región OpenMP) que extraen, calculan la edad con `edad::calcular(const std::string*)`,
 *   discretizan por truncamiento (años enteros) y agregan en un `boost::unordered::concurrent_flat_map<int,int>`.
 *
 * ### Fundamentación técnica
 * - **Lock-free vs wait-free:** `boost::lockfree::queue` provee *progreso lock-free* (al menos un hilo progresa bajo contención);
 *   no es *wait-free* (no garantiza progreso en pasos finitos para cada hilo). El *backoff* (yield) atenúa la contención.
 * - **Tipo trivial (T)**: para ser elegible en `lockfree::queue<T>`, `T` debe ser *trivially copyable*; por eso se encolan
 *   **punteros crudos** (`std::string*`) y no `std::string` (ver @ref Memoria).
 * - **Linealizabilidad:** `push`/`pop` son operaciones atómicas linealizables; el mapa `concurrent_flat_map` expone
 *   métodos `try_emplace/visit` que aseguran exclusión por clave durante la mutación del valor.
 * - **Modelo de memoria:** usamos `std::memory_order_release/acquire` para el *flag* `terminado`. Esto establece un *happens-before*
 *   entre el último `store(release)` del productor y el correspondiente `load(acquire)` del consumidor, garantizando visibilidad
 *   de la finalización. Para contadores del histograma (si se usara `std::atomic<int>` por clave) bastaría `memory_order_relaxed`
 *   al no requerir orden global, pero aquí el contenedor concurrente encapsula su propia sincronización.
 *
 * ### Complejidad
 * - Lectura y encolado: **O(N)** en número de líneas (latencia amortizada de `push` lock-free).
 * - Procesamiento: **O(N)**; actualización del mapa es **O(1) amortizado** por inserción/acceso por clave entera en un dominio
 *   acotado ([0,130]). El coste real depende de colisiones/buckets y de la política de `concurrent_flat_map`.
 *
 * ### Escalabilidad y performance
 * - **Contención**: los *hot keys* (edades frecuentes, p.ej., 18) concentran accesos y elevan la latencia en `visit`.
 *   En datos muy sesgados: considerar *sharding* por hilo y *merge* final (ver @ref Optimizaciones).
 * - **NUMA**: si se ejecuta en sockets múltiples, fijar afinidad o usar partición por nodo para minimizar *remote misses*.
 * - **False sharing**: evitado en la cola (controlada por Boost). En el mapa, la localización de `value_type` y `bucket` es interna.
 * - **Truncamiento**: `static_cast<int>(edad)` introduce **sesgo hacia abajo** frente a *floor* con negativos; como solo se aceptan
 *   edades >= 0, el sesgo es el del truncamiento puro (ver @ref Discretizacion).
 *
 * ### Compilación (ejemplos)
 * @code{.bash}
 * g++ -std=c++17 -O3 -fopenmp main.cpp -lboost_thread -lboost_system -o programa
 * @endcode
 *
 * ### Ejecución
 * @code{.bash}
 * OMP_NUM_THREADS=8 ./programa datos.csv
 * @endcode
 *
 * @section ContratoEdad Contrato con `Edad.h`
 * Se espera una API *thread-safe*:
 * @code
 * namespace edad {
 *   // Devuelve NaN si no puede calcular; no lanza (o captura internamente).
 *   double calcular(const std::string* linea);
 * }
 * @endcode
 *
 * @section FormatoEntrada Formato de entrada típico
 * Línea con fecha interpretable por `edad::calcular`, p.ej. CSV:
 * @code
 * 2004-11-01
 * 2005-01-06
 * @endcode
 *
 *
 * @section Glosario Glosario breve
 * - **MPMC**: Multi-Producer Multi-Consumer. Aquí usamos *productor único*, *consumidor múltiple* (S-PMC).
 * - **Linealizabilidad**: cada operación concurrente aparenta ocurrir en un instante atómico total.
 * - **Lock-free**: el sistema progresa aunque hilos individuales se bloqueen o fallen.
 * - **Wait-free**: cada operación finaliza en pasos finitos (no garantizado aquí).
 */

#include <iostream>
#include <string>
#include <cstdlib>
#include <boost/lockfree/queue.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>
#include <atomic>
#include <optional>
#include <fstream>
#include <thread>
#include <vector>
#include <omp.h>
#include <cmath>

#include "Edad.h"

/**
 * @defgroup cli Interfaz de Línea de Comandos
 * @brief Entradas/salidas del ejecutable y créditos.
 * @{
 */

/**
 * @brief Muestra los participantes/créditos del proyecto y contexto académico.
 *
 * Imprime en @c stdout un encabezado con el nombre del programa y los créditos del taller.
 *
 * @param programa Nombre del ejecutable (típicamente @c argv[0]).
 * @post Emite varias líneas en la salida estándar con información de créditos.
 * @exception Ninguna. (Los streams no lanzan por defecto; si están en modo con excepciones, capturar externamente).
 *
 * @code
 * participantes("programa");
 * // === programa :: Programa de ejemplo de uso de OpenMP ===
 * // Computación paralela y distribuida
 * // Universidad Tecnológica Metropolitana
 * // Académico Sebastián Salazar Molina.
 * @endcode
 */
void participantes(std::string programa);

/** @} */ // end of group cli

/**
 * @addtogroup cli
 * @{
 */

/**
 * @brief Punto de entrada: productor–consumidor con OpenMP, `boost::lockfree::queue` y `concurrent_flat_map`.
 *
 * @param argc Cantidad de argumentos (incluye el nombre del programa).
 * @param argv Vector de argumentos. Si @c argc > 1, @c argv[1] es la ruta del archivo a procesar.
 * @return `EXIT_SUCCESS` en todos los caminos; si no hay ruta, muestra créditos y retorna éxito.
 *
 * @pre Si @c argc > 1, `argv[1]` debe ser una ruta válida y legible.
 * @post Si se procesó archivo, emite en @c stdout el número de ocurrencias por edad (una línea por clave).
 *
 * ### Detalles de sincronización
 * - **Fin de producción**: `terminado.store(true, std::memory_order_release)` al completar la lectura.
 * - **Consumo**: tras `terminado.load(memory_order_acquire)` y `cola.empty()` se garantiza que no llegarán más elementos.
 * - **Backoff**: `std::this_thread::yield()` como espera cooperativa; en cargas CPU-bound considerar *spin-then-park*.
 *
 * @remark `concurrent_flat_map::visit` asegura exclusión por clave (no por mapa completo), reduciendo contención
 *         respecto a un `std::unordered_map` + `#pragma omp critical`.
 */
int main(int argc, char** argv) {
    if (argc > 1) {
        const std::string ruta = argv[1];

        /// Capacidad de la cola: potencia de 2 suele mejorar el rendimiento de estructuras lock-free por alineación y máscaras.
        const std::size_t capacidad = 131072u;

        /**
         * @brief Cola lock-free MPMC de punteros a `std::string`.
         * @details
         * - Tipo trivial requerido ⇒ se usan punteros crudos.
         * - Productor único (`single nowait`), múltiples consumidores (resto de hilos).
         * - **Propiedad de memoria**: cada puntero encolado debe ser liberado exactamente una vez por un consumidor.
         */
        boost::lockfree::queue<std::string*> cola(capacidad);

        /**
         * @brief Mapa concurrente (edad → ocurrencias).
         * @details
         * - `try_emplace(clave, 0)`: crea la entrada si no existe (valor inicial 0).
         * - `visit(clave, lambda)`: sección crítica fina por clave; el *lambda* ve un `value_type&`.
         * - Buckets iniciales: 4096 para minimizar *rehash* bajo alta concurrencia.
         * @note Evitar iteradores persistentes: pueden invalidarse tras rehash interno.
         */
        boost::unordered::concurrent_flat_map<int, int> mapa(4096);

        /// Señal de finalización del productor. `release/acquire` garantiza visibilidad del fin a consumidores.
        std::atomic<bool> terminado{false};

#pragma omp parallel
        {
            // PRODUCTOR ÚNICO: lee y encola; los demás hilos consumen en paralelo (nowait evita barrera).
#pragma omp single nowait
            {
                std::ifstream archivo(ruta);
                if (!archivo) {
                    std::cerr << "No se pudo abrir: " << ruta << "\n";
                    terminado.store(true, std::memory_order_release);
                } else {
                    std::string linea;
                    while (std::getline(archivo, linea)) {
                        // Reserva en heap y mueve el contenido para evitar copia.
                        std::string *p = new std::string(std::move(linea));
                        // Encolar con backoff si la cola está temporalmente llena.
                        while (!cola.push(p)) {
                            std::this_thread::yield();
                        }
                        // 'linea' queda reutilizable para el siguiente getline.
                    }
                    terminado.store(true, std::memory_order_release);
                }
            }

            // CONSUMIDORES: todos los hilos (incluido el del single tras terminar la lectura).
            for (;;) {
                std::string* fecha = nullptr;
                if (cola.pop(fecha)) {
                    // --- Procesamiento de la línea ---
                    if (fecha && !fecha->empty()) {
                        // cálculo de edad decimal desde la línea (p. ej., parseo de YYYY-MM-DD)
                        double e = edad::calcular(*fecha); // contrato: thread-safe, devuelve NaN en fallo
                        if (!std::isnan(e) && e >= 0.0) {
                            // Discretización por truncamiento (años completos).
                            int clave = static_cast<int> (e);
                            if (clave >= 0 && clave <= 130) { // cota razonable/empírica
                                // Asegurar existencia y sumar de forma thread-safe por clave.
                                mapa.try_emplace(clave, 0);
                                mapa.visit(clave, [](boost::unordered::concurrent_flat_map<int, int>::value_type & par) {
                                    ++par.second; // incremento atómico bajo exclusión por clave
                                });
                            }
                        }
                    }
                    delete fecha; // IMPORTANTÍSIMO: liberar SIEMPRE la memoria de la línea consumida
                } else {
                    // Terminar si ya no habrá más producción y la cola está vacía.
                    if (terminado.load(std::memory_order_acquire) && cola.empty()) {
                        break;
                    }
                    std::this_thread::yield();
                }
            }
        }

        // Emisión de resultados (secuencial, una vez fuera de la región paralela).
        mapa.visit_all([](const boost::unordered::concurrent_flat_map<int, int>::value_type & par) {
            std::cout << "La edad " << par.first << " tiene " << par.second << " ocurrencias\n";
        });

    } else {
        participantes(argv[0]);
    }

    return EXIT_SUCCESS;
}

/** @} */ // end of group cli

/**
 * @brief Implementación que imprime créditos del programa.
 * @param programa Nombre del ejecutable a mostrar en el encabezado.
 * @see participantes(std::string)
 */
void participantes(std::string programa) {
    std::cout << std::endl << "=== " << programa << " :: Programa de ejemplo de uso de OpenMP ===" << std::endl;
    std::cout << std::endl << "Computación paralela y distribuida";
    std::cout << std::endl << "Universidad Tecnológica Metropolitana";
    std::cout << std::endl << "Académico Sebastián Salazar Molina." << std::endl;
}
