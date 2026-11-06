// Mapeamento dos 16 bits do controle SNES -> direção de cada servo do braço.
//
// Tabela da wiki "Braço Robótico ‐ Semana 09\09‐21\10" (William):
//   ← / →  girar a base          B  sobe o cotovelo     A  abre a garra
//   ↑ / ↓  sobe / desce o ombro  Y  desce o cotovelo    X  fecha a garra
//   Start  para os movimentos (enquanto pressionado)
//   Select modo acelerômetro: tratado no firmware do joystick, aqui não faz nada
//
// C++ puro (sem Arduino): o mesmo arquivo é usado pelo firmware, pelos testes do
// snes_arm_daemon e pelo frame_sniffer. O simulador (arm_sim.py) replica esta tabela.
#pragma once
#include <stdint.h>

namespace ArmMapping {

// Bits do registrador SNES (SnesState.h do joystick / protocol_spec.md §4). Ativo em LOW.
enum Bit : uint8_t {
    B = 0, Y = 1, SELECT = 2, START = 3,
    UP = 4, DOWN = 5, LEFT = 6, RIGHT = 7,
    A = 8, X = 9, L = 10, R = 11,
};

constexpr uint16_t ALL_RELEASED = 0xFFFF;

// -1, 0 ou +1 para cada servo, como no fluxograma (dirBase, dirOmbro, dirCotovelo, dirGarra).
struct Directions {
    int8_t base;
    int8_t ombro;
    int8_t cotovelo;
    int8_t garra;
};

inline bool pressed(uint16_t state, Bit bit) { return (state & (1u << bit)) == 0; }

// Os dois sentidos juntos se anulam (mesma regra do D-pad no snes_input_daemon).
inline int8_t axis(bool plus, bool minus) { return plus == minus ? 0 : (plus ? 1 : -1); }

inline Directions fromState(uint16_t state) {
    if (pressed(state, START)) return {0, 0, 0, 0};  // Start: parar os movimentos
    Directions d;
    d.base = axis(pressed(state, RIGHT), pressed(state, LEFT));
    d.ombro = axis(pressed(state, UP), pressed(state, DOWN));
    d.cotovelo = axis(pressed(state, B), pressed(state, Y));
    d.garra = axis(pressed(state, A), pressed(state, X));
    return d;
}

}  // namespace ArmMapping
