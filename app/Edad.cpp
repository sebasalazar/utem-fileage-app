#include "Edad.h"

long long edad::fecha_a_dias( long long anio, unsigned int mes, unsigned int dia) noexcept {
    anio -= mes <= 2;
    const long long era = (anio >= 0 ? anio : anio - 399) / 400;
    const unsigned anio_era = static_cast<unsigned> (anio - era * 400);
    const unsigned dia_anio = (153 * (mes + (mes > 2 ? -3 : 9)) + 2) / 5 + dia - 1;
    const unsigned dias_era = anio_era * 365 + anio_era / 4 - anio_era / 100 + dia_anio;
    return era * 146097 + static_cast<long long> (dias_era) - 719468; // 719468 = días hasta 1970-01-01
}

std::tuple<int, int, int> edad::parsear_fecha_iso(const std::string& texto) {
    if (texto.size() != 10 || texto[4] != '-' || texto[7] != '-') {
        throw std::invalid_argument("Formato inválido; se espera YYYY-MM-DD");
    }
    int anio = std::stoi(texto.substr(0, 4));
    int mes = std::stoi(texto.substr(5, 2));
    int dia = std::stoi(texto.substr(8, 2));
    return {anio, mes, dia};
}

double edad::calcular(const std::string& fecha_nacimiento) {
    // Parseo de la fecha de nacimiento
    int anio_nac, mes_nac, dia_nac;
    std::tie(anio_nac, mes_nac, dia_nac) = edad::parsear_fecha_iso(fecha_nacimiento);
    const long long dias_nacimiento = edad::fecha_a_dias(anio_nac, mes_nac, dia_nac);

    // Fecha actual
    std::time_t tiempo = std::time(nullptr);
    std::tm fecha_actual{};
#ifdef _WIN32
    localtime_s(&fecha_actual, &tiempo);
#else
    localtime_r(&tiempo, &fecha_actual);
#endif
    const long long dias_hoy = edad::fecha_a_dias(
            static_cast<long long> (fecha_actual.tm_year + 1900),
            static_cast<unsigned> (fecha_actual.tm_mon + 1),
            static_cast<unsigned> (fecha_actual.tm_mday)
            );

    // Cálculo de la edad
    constexpr double DIAS_PROMEDIO_ANIO = 365.2425;
    return static_cast<double> (dias_hoy - dias_nacimiento) / DIAS_PROMEDIO_ANIO;
}