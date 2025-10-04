/**
 * @file
 * @brief Taller computacional: lectura concurrente de archivo y cómputo de histograma de edades con OpenMP.
 *
 * Este programa ilustra un patrón productor–consumidor usando **OpenMP tasks**:
 * un hilo lee líneas de un archivo de entrada (CSV o texto simple, una persona por línea) y crea
 * tareas; los demás hilos consumen esas tareas para calcular la edad (vía `edad::calcular`)
 * y actualizar un histograma atómico de 0..130 años.
 *
 * ## Idea general
 * - Se inicializa un @ref histograma "histograma" con 131 contadores atómicos (0..130).
 * - En una región paralela, una sección `single` abre el archivo y, por cada línea,
 *   crea una `#pragma omp task` que:
 *   - Calcula la edad decimal con `edad::calcular`.
 *   - Trunca a entero y, si está en rango [0,130], incrementa el contador correspondiente.
 * - Al final, se imprime de forma determinística cada edad con ocurrencias > 0.
 *
 * ## Concurrencia y orden de memoria
 * - Los contadores usan `std::memory_order_relaxed`, válido porque cada índice
 *   del histograma es independiente y solo necesitamos suma atómica sin orden global.
 *
 * ## Rendimiento
 * - **Cache-friendly**: arreglo contiguo de `std::atomic<int>` para reducir falsos
 *   compartidos en accesos dispersos por edad.
 * - **Granularidad de tasks**: una línea = una tarea; para archivos muy grandes podrías
 *   agrupar líneas por bloques (TODO).
 *
 * @par Requisitos
 * - Compilador C++17 o superior.
 * - OpenMP habilitado.
 * - Implementación del header `"Edad.h"` que provea `namespace edad { double calcular(const std::string*); }`.
 *
 * @par Compilación (ejemplos)
 * @code{.bash}
 * g++ -std=c++17 -O2 -fopenmp main.cpp -o programa
 * clang++ -std=c++17 -O2 -fopenmp main.cpp -o programa
 * @endcode
 *
 * @par Ejecución (ejemplos)
 * @code{.bash}
 * ./programa datos.csv
 * OMP_NUM_THREADS=8 ./programa /ruta/a/datos.csv
 * @endcode
 *
 * @par Formato de entrada esperado
 * El programa lee el archivo línea a línea. Cada línea debe contener la información suficiente
 * para que `edad::calcular` pueda obtener una edad (por ejemplo, un CSV con una columna de fecha).
 * Si una línea es inválida o no se puede calcular edad, simplemente se ignora.
 *
 * @par Ejemplo de líneas (sugerencia)
 * @code
 * 2004-11-01
 * 2005-01-06
 * @endcode
 *
 * @note La función @ref participantes imprime créditos y se invoca cuando no se entrega ruta de archivo.
 * @warning Este programa no valida que el archivo tenga encabezados ni columnas específicas; la
 *          responsabilidad de parseo está encapsulada en `edad::calcular`.
 * @see participantes, main
 */

#include <iostream>
#include <string>
#include <fstream>
#include <array>
#include <atomic>
#include <optional>
#include <cstddef>
#include <omp.h>
#include <cmath>

#include "Edad.h"

/**
 * @defgroup cli Interfaz de Línea de Comandos
 * @brief Entradas y salidas del ejecutable.
 * @{
 */

/**
 * @brief Muestra los participantes/créditos del proyecto y contexto académico.
 *
 * Imprime en @c stdout un encabezado con el nombre del programa y los créditos del taller.
 *
 * @param programa Nombre del ejecutable (habitualmente @c argv[0]).
 *
 * @post Se escriben varias líneas en la salida estándar con información de créditos.
 * @exception Ninguna. (Operaciones de E/S pueden fallar silenciosamente en streams, pero no se lanzan excepciones.)
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
 * @brief Punto de entrada del programa.
 *
 * Lógica principal del taller:
 * - Si no se recibe ruta de archivo, imprime @ref participantes y finaliza con éxito.
 * - Si se entrega ruta, procesa el archivo concurrentemente con OpenMP:
 *   - Inicializa @ref histograma de 0..130.
 *   - Crea una región paralela con una sección `single` que lee línea a línea y
 *     crea @c tasks para calcular edades y actualizar contadores.
 *   - Espera la finalización de tareas y emite el histograma no nulo.
 *
 * @param argc Cantidad de argumentos (incluye el nombre del programa).
 * @param argv Vector de argumentos. Se espera que @c argv[1] sea la ruta al archivo a procesar.
 * @return `EXIT_SUCCESS` si el flujo general se completa. En caso de error al abrir archivo, igual retorna éxito,
 *         pero informa por @c std::cerr; las líneas inválidas se ignoran.
 *
 * @pre Si @c argc > 1, entonces @c argv[1] debe ser una ruta válida o, al menos, accesible para apertura de lectura.
 * @post Se imprime en @c stdout cada edad con su número de ocurrencias (@c > 0), una línea por edad.
 *
 * @par Seguridad en hilos
 * - El @ref histograma usa operaciones atómicas relajadas por independencia de celdas.
 * - No hay datos compartidos mutables adicionales entre tareas (salvo el arreglo atómico).
 *
 * @par Variables de entorno útiles
 * - `OMP_NUM_THREADS`: define el número de hilos para la región paralela.
 *
 * @todo (Optimizaciones futuras) Agrupar líneas en bloques para reducir overhead de creación de tasks cuando el archivo es muy grande.
 * @todo (Robustez) Añadir conteo de líneas inválidas y métricas de procesamiento (tiempo total, tareas creadas, etc.).
 */
int main(int argc, char** argv) {
    if (argc <= 1) {
        participantes(std::string(argv[0] != nullptr ? argv[0] : "programa"));
        return EXIT_SUCCESS;
    }

    const std::string ruta = std::string(argv[1]);

    /**
     * @brief Histograma global de edades (0..130).
     *
     * Arreglo contiguo de 131 contadores @c std::atomic<int>. Cada índice representa una edad
     * entera (años cumplidos por truncamiento de edad decimal). Se inicializa a cero antes de
     * procesar el archivo.
     *
     * @invariant @c histograma.size() == 131
     * @thread_safety Seguro para acceso concurrente mediante @c fetch_add y @c load.
     */
    std::array<std::atomic<int>, 131> histograma;
    for (std::size_t i = 0u; i < histograma.size(); ++i) {
        histograma[i].store(0, std::memory_order_relaxed);
    }

    // Región paralela: un hilo lee, crea tasks; todos consumen tasks
#pragma omp parallel default(none) shared(ruta, histograma, std::cerr)
    {
#pragma omp single
        {
            std::ifstream archivo(ruta);
            if (!archivo) {
                std::cerr << "No se pudo abrir el archivo: " << ruta << "\n";
            } else {
                std::string linea;

                while (std::getline(archivo, linea)) {
#pragma omp task firstprivate(linea) shared(histograma)
                    {
                        if (!linea.empty()) {
                            // Se delega a 'edad::calcular' la interpretación de la línea.
                            // Nota: 'edad::calcular' debe ser thread-safe para invocaciones concurrentes.
                            std::string *p = new std::string(std::move(linea));
                            const double edad_decimal = edad::calcular(*p);
                            const bool edad_valida = (!std::isnan(edad_decimal) && edad_decimal >= 0.0);
                            if (edad_valida) {
                                const int clave = static_cast<int> (edad_decimal); // trunc
                                const bool dentro_rango = (clave >= 0 && clave <= 130);
                                if (dentro_rango) {
                                    // Un contador independiente por edad -> relaxed está perfecto
                                    histograma[static_cast<std::size_t> (clave)].fetch_add(1, std::memory_order_relaxed);
                                }
                            }
                        }
                    } // task
                } // while getline

#pragma omp taskwait
            } // if archivo
        } // single
    } // parallel

    // Salida ordenada y determinística
    for (int edad = 0; edad <= 130; ++edad) {
        const int cuenta = histograma[static_cast<std::size_t> (edad)].load(std::memory_order_relaxed);
        if (cuenta != 0) {
            std::cout << "La edad " << edad << " tiene " << cuenta << " ocurrencias\n";
        }
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
