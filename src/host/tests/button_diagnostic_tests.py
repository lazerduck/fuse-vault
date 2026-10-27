"""Exercise the real Pico button adapter with simulated GPIO and time."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
STUB = r'''
#include <stdint.h>
#define GPIO_IN 0
#define FUSE_VAULT_NAV_UP_PIN 10
#define FUSE_VAULT_NAV_LEFT_PIN 11
#define FUSE_VAULT_NAV_DOWN_PIN 12
#define FUSE_VAULT_NAV_RIGHT_PIN 13
#define FUSE_VAULT_NAV_SELECT_PIN 14
#define FUSE_VAULT_NAV_BACK_PIN 15
extern uint32_t fake_gpio;
extern uint64_t fake_now;
static inline void gpio_init(unsigned pin) {(void)pin;}
static inline void gpio_set_dir(unsigned pin, unsigned dir) {(void)pin;(void)dir;}
static inline void gpio_disable_pulls(unsigned pin) {(void)pin;}
static inline uint32_t gpio_get_all(void) {return fake_gpio;}
static inline uint64_t time_us_64(void) {return fake_now;}
'''
TEST = r'''
#include <assert.h>
#include "fuse_vault/pico_buttons.h"
uint32_t fake_gpio = ~0u;
uint64_t fake_now;
static void sample(unsigned mask, uint64_t now) {
    fake_gpio = ~(mask << 10);
    fake_now = now;
    (void)fv_pico_buttons_poll(0);
}
int main(void) {
    fv_pico_buttons_init();
    sample(0, 30000);
    assert(fv_pico_buttons_diagnostics().raw_pressed == 0);
    for (unsigned i = 0; i < 6; ++i) {
        uint64_t t = 100000 + i * 100000;
        sample(1u << i, t);
        assert(fv_pico_buttons_diagnostics().raw_pressed == (1u << i));
        assert(fv_pico_buttons_diagnostics().stable_pressed == 0);
        sample(1u << i, t + 25000);
        assert(fv_pico_buttons_diagnostics().stable_pressed == (1u << i));
        assert(fv_pico_buttons_diagnostics().presses[i] == 1);
        sample(1u << i, t + 30000);
        assert(fv_pico_buttons_diagnostics().presses[i] == 1);
        sample(0, t + 40000);
        sample(0, t + 65000);
        assert(fv_pico_buttons_diagnostics().stable_pressed == 0);
    }
    /* Boot with all inputs low: capture every input even though UI rejects chords. */
    fv_pico_buttons_init();
    sample(63, 800000);
    sample(63, 825000);
    fv_button_diagnostics d = fv_pico_buttons_diagnostics();
    assert(d.stable_pressed == 63);
    for (unsigned i = 0; i < 6; ++i) assert(d.presses[i] == 1);
    /* A bouncing Up input must not prevent Select being detected. */
    fv_pico_buttons_init();
    sample(16, 900000);
    sample(17, 910000);
    sample(16, 920000);
    sample(17, 930000);
    d = fv_pico_buttons_diagnostics();
    assert(d.stable_pressed == 16 && d.presses[4] == 1 && d.presses[0] == 0);
    sample(0, 940000);
    sample(0, 965000);
    sample(16, 970000);
    sample(16, 995000);
    assert(fv_pico_buttons_diagnostics().presses[4] == 2);
    return 0;
}
'''

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'pico').mkdir()
    (path / 'pico/stdlib.h').write_text(STUB)
    (path / 'test.c').write_text(TEST)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-DFV_DEBUG_BUTTONS=1', '-I' + str(path),
                    '-I' + str(ROOT / 'src/pico/platform/include'),
                    '-I' + str(ROOT / 'src/shared/ui'),
                    str(path / 'test.c'), str(ROOT / 'src/pico/platform/buttons.c'),
                    str(ROOT / 'src/shared/ui/button_input.c'),
                    '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('PASS: six inputs, debounce, held/released, simultaneous, boot-low and noisy neighbour')
