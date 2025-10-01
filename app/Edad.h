#ifndef EDAD_H
#define EDAD_H

/**
 * @file edad.hpp
 * @brief Cálculo de edad en años decimales a partir de una fecha de nacimiento (formato ISO YYYY-MM-DD).
 *
 * @details
 * La edad se calcula como:
 * \f[
 *   \text{edad} = \frac{\text{días transcurridos}}{365.2425}
 * \f]
 * donde 365.2425 representa la duración promedio de un año astronómico (incluye
 * los años bisiestos en promedio).
 *
 * Ventajas:
 *   - Código sencillo y mantenible.
 *   - Evita cálculos manuales de meses y días.
 *
 * Limitaciones:
 *   - La fracción decimal es un **promedio anual** y no corresponde exactamente
 *     al porcentaje transcurrido entre el último y próximo cumpleaños.
 */

#include <iostream>
#include <string>
#include <sstream>
#include <chrono>
#include <ctime>


namespace edad {

    /**
     * @brief Convierte una fecha civil (año, mes, día) a un contador de días desde una época fija.
     *
     * @param anio Año (ejemplo: 2005).
     * @param mes Mes en rango [1..12].
     * @param dia Día en rango [1..31].
     * @return Número de días transcurridos desde una época fija (puede ser negativo).
     *
     * @details
     * Implementación basada en el algoritmo de Howard Hinnant (dominio público).
     * La época exacta no importa mientras se comparen dos fechas con la misma base.
     */
    long long fecha_a_dias( long long anio, unsigned int mes, unsigned int dia) noexcept;

    /**
     * @brief Parsea una fecha en formato ISO "YYYY-MM-DD".
     *
     * @param texto Fecha como cadena (ejemplo: "2005-01-06").
     * @return Tupla con (anio, mes, dia).
     * @throws std::invalid_argument Si el formato no es válido.
     */
    std::tuple<int, int, int> parsear_fecha_iso(const std::string& texto);

    /**
     * @brief Calcula la edad en años decimales a partir de la fecha de nacimiento.
     *
     * @param fecha_nacimiento Fecha de nacimiento en formato ISO "YYYY-MM-DD".
     * @return Edad en años con fracción decimal (ejemplo: 20.75 aprox 20 años y 9 meses).
     * @throws std::invalid_argument Si la fecha no respeta el formato ISO.
     *
     * @note La fracción decimal es una aproximación basada en el año promedio
     *       (365.2425 días). No corresponde exactamente al tiempo transcurrido
     *       entre cumpleaños.
     *
     * @code{.cpp}
     * #include "edad.hpp"
     * #include <iostream>
     *
     * int main() {
     *     try {
     *         double edad = edad::calcular("2005-01-06");
     *         std::cout << "Edad: " << edad << " años\n";
     *     } catch (const std::exception& ex) {
     *         std::cerr << "Error: " << ex.what() << '\n';
     *     }
     * }
     * @endcode
     */
    double calcular(const std::string& fecha_nacimiento);
}

#endif /* EDAD_H */

