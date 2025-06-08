/**
 * @file main.c
 * @brief Sintetizador de áudio para gravação e reprodução em Raspberry Pi Pico.
 * @author Gabriel da Conceição Miranda
 * @date 07/06/2025
 *
 * Este projeto captura áudio do microfone, aplica um filtro passa-baixa,
 * armazena em um buffer e o reproduz via PWM. Inclui feedback visual
 * com LED RGB e visualização da forma de onda em um display OLED.
 */

// =================================================================================
// Inclusões de Bibliotecas
// =================================================================================
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/types.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "inc/ssd1306.h" // Lib externa para o display OLED

// =================================================================================
// Definições e Constantes do Projeto
// =================================================================================

// --- Mapeamento de Pinos ---
#define PINO_BOTAO_GRAVAR 5
#define PINO_BOTAO_REPRODUZIR 6
#define PINO_LED_VERMELHO 13
#define PINO_LED_VERDE 11
#define PINO_LED_AZUL 12
#define PINO_I2C_SDA 14
#define PINO_I2C_SCL 15
#define PINO_BUZZER_1 21
#define PINO_BUZZER_2 10
#define CANAL_ADC_MIC 2
#define PINO_MICROFONE (26 + CANAL_ADC_MIC) // GP28

// --- Parâmetros do Display ---
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

// --- Parâmetros de Áudio ---
#define TAXA_AMOSTRAGEM 48000
#define DURACAO_GRAVACAO_S 2
#define TAMANHO_BUFFER_AUDIO (TAXA_AMOSTRAGEM * DURACAO_GRAVACAO_S)
#define FATOR_SUAVIZACAO 0.2f // Alpha para o filtro
#define GANHO_SAIDA_AUDIO 1.7f

// --- Parâmetros Gerais ---
#define TEMPO_DEBOUNCE_BOTAO_MS 200

// =================================================================================
// Variáveis Globais
// =================================================================================

// Buffers de dados
uint16_t buffer_de_amostras[TAMANHO_BUFFER_AUDIO];
uint8_t frame_buffer_display[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];

// Flags de interrupção para os botões
volatile bool flag_botao_gravar_ativado = false;
volatile bool flag_botao_reproduzir_ativado = false;

// Controle de tempo para debounce
static uint32_t ultimo_acionamento_gravar = 0;
static uint32_t ultimo_acionamento_reproduzir = 0;

// Configuração do DMA
uint canal_dma_adc;
dma_channel_config config_canal_dma;

// =================================================================================
// Protótipos de Funções (Declarações Antecipadas)
// =================================================================================

// --- Inicialização ---
void inicializar_perifericos_basicos();
void inicializar_leds();
void configurar_botoes_com_interrupcao();
void inicializar_adc_e_dma();
void configurar_saida_pwm(uint pino, uint32_t frequencia_base);
void inicializar_comunicacao_i2c();

// --- Lógica Principal ---
size_t processo_de_gravacao(uint32_t freq_amostragem, uint32_t duracao_seg);
void processo_de_reproducao(uint pino_a, uint pino_b, uint16_t *dados, size_t n_amostras, uint32_t freq_amostragem);

// --- Funções de Apoio e Utilitários ---
void definir_cor_led(bool vermelho, bool verde, bool azul);
void tratador_interrupcao_botao(uint pino, uint32_t eventos);
void capturar_amostras_via_dma(uint16_t *destino, size_t n_amostras);
uint16_t suavizar_sinal_audio(uint16_t amostra_atual, uint16_t amostra_anterior, float fator);
void mostrar_waveform_display(uint8_t *buffer_tela, uint16_t *dados_audio, size_t n_amostras);
void apagar_tela();

// =================================================================================
// Função Principal (main)
// =================================================================================

int main() {
    inicializar_perifericos_basicos();

    // --- Máquina de Estados do Sistema ---
    enum { MODO_ESPERA, MODO_AGUARDANDO_PLAYBACK } estado_do_sistema = MODO_ESPERA;
    size_t total_amostras_capturadas = 0;

    printf("Sintetizador de Áudio iniciado. Aguardando comando.\n");

    while (true) {
        switch (estado_do_sistema) {
            case MODO_ESPERA:
                if (flag_botao_gravar_ativado) {
                    flag_botao_gravar_ativado = false; // Reseta a flag

                    definir_cor_led(1, 0, 0); // LED Vermelho: Gravando
                    printf("Iniciando gravação...\n");
                    total_amostras_capturadas = processo_de_gravacao(TAXA_AMOSTRAGEM, DURACAO_GRAVACAO_S);
                    definir_cor_led(0, 0, 0); // LED Desligado

                    printf("Gravação concluída. Desenhando forma de onda.\n");
                    mostrar_waveform_display(frame_buffer_display, buffer_de_amostras, total_amostras_capturadas);
                    
                    estado_do_sistema = MODO_AGUARDANDO_PLAYBACK;
                    printf("Pronto para reproduzir. Pressione o outro botão.\n");
                }
                break;

            case MODO_AGUARDANDO_PLAYBACK:
                if (flag_botao_reproduzir_ativado) {
                    flag_botao_reproduzir_ativado = false; // Reseta a flag

                    definir_cor_led(0, 1, 0); // LED Verde: Reproduzindo
                    printf("Iniciando reprodução...\n");
                    processo_de_reproducao(PINO_BUZZER_1, PINO_BUZZER_2, buffer_de_amostras, total_amostras_capturadas, TAXA_AMOSTRAGEM);
                    definir_cor_led(0, 0, 0); // LED Desligado

                    printf("Reprodução concluída. Reiniciando ciclo.\n\n");
                    apagar_tela();
                    
                    // CORREÇÃO: Limpa qualquer flag de gravação que possa ter sido ativada durante a reprodução
                    flag_botao_gravar_ativado = false; 
                    
                    estado_do_sistema = MODO_ESPERA;
                }
                break;
        }
        sleep_ms(10); // Pequena pausa para economizar processamento
    }
}

// =================================================================================
// Implementação das Funções
// =================================================================================

// ------------------------
// --- Inicialização ---
// ------------------------

void inicializar_perifericos_basicos() {
    stdio_init_all();
    sleep_ms(2000); // Pausa para permitir a conexão do terminal serial

    inicializar_leds();
    definir_cor_led(0, 0, 0); // Garante que os LEDs comecem apagados

    configurar_botoes_com_interrupcao();
    inicializar_adc_e_dma();

    configurar_saida_pwm(PINO_BUZZER_1, TAXA_AMOSTRAGEM);
    configurar_saida_pwm(PINO_BUZZER_2, TAXA_AMOSTRAGEM);
    
    inicializar_comunicacao_i2c();
    ssd1306_init(); // Inicializa o controlador do display
    apagar_tela();
}

void inicializar_leds() {
    gpio_init(PINO_LED_VERMELHO);
    gpio_init(PINO_LED_VERDE);
    gpio_init(PINO_LED_AZUL);
    gpio_set_dir(PINO_LED_VERMELHO, GPIO_OUT);
    gpio_set_dir(PINO_LED_VERDE, GPIO_OUT);
    gpio_set_dir(PINO_LED_AZUL, GPIO_OUT);
}

void configurar_botoes_com_interrupcao() {
    gpio_init(PINO_BOTAO_GRAVAR);
    gpio_set_dir(PINO_BOTAO_GRAVAR, GPIO_IN);
    gpio_pull_up(PINO_BOTAO_GRAVAR);

    gpio_init(PINO_BOTAO_REPRODUZIR);
    gpio_set_dir(PINO_BOTAO_REPRODUZIR, GPIO_IN);
    gpio_pull_up(PINO_BOTAO_REPRODUZIR);

    // Configura a mesma função de callback para ambos os botões
    gpio_set_irq_enabled_with_callback(PINO_BOTAO_GRAVAR, GPIO_IRQ_EDGE_FALL, true, &tratador_interrupcao_botao);
    gpio_set_irq_enabled_with_callback(PINO_BOTAO_REPRODUZIR, GPIO_IRQ_EDGE_FALL, true, &tratador_interrupcao_botao);
}

void inicializar_adc_e_dma() {
    // ADC
    adc_gpio_init(PINO_MICROFONE);
    adc_init();
    adc_select_input(CANAL_ADC_MIC);
    adc_fifo_setup(true, true, 1, false, false); // Habilita FIFO para DMA

    // DMA
    canal_dma_adc = dma_claim_unused_channel(true);
    config_canal_dma = dma_channel_get_default_config(canal_dma_adc);
    channel_config_set_transfer_data_size(&config_canal_dma, DMA_SIZE_16);
    channel_config_set_read_increment(&config_canal_dma, false);
    channel_config_set_write_increment(&config_canal_dma, true);
    channel_config_set_dreq(&config_canal_dma, DREQ_ADC);
}

void configurar_saida_pwm(uint pino, uint32_t frequencia_base) {
    gpio_set_function(pino, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(pino);

    uint32_t clock = clock_get_hz(clk_sys);
    uint32_t top = clock / frequencia_base - 1;
    if (top == 0) top = 1;

    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, top);
    pwm_init(slice_num, &config, true);
    pwm_set_gpio_level(pino, 0); // Inicia com o som desligado
}

void inicializar_comunicacao_i2c() {
    i2c_init(i2c1, ssd1306_i2c_clock * 1000);
    gpio_set_function(PINO_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PINO_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PINO_I2C_SDA);
    gpio_pull_up(PINO_I2C_SCL);
}

// ---------------------------
// --- Lógica Principal ---
// ---------------------------

size_t processo_de_gravacao(uint32_t freq_amostragem, uint32_t duracao_seg) {
    size_t total_de_amostras = freq_amostragem * duracao_seg;
    if (total_de_amostras > TAMANHO_BUFFER_AUDIO) {
        total_de_amostras = TAMANHO_BUFFER_AUDIO;
    }

    float divisor_clock_adc = (float)clock_get_hz(clk_adc) / (freq_amostragem * 1.0f);
    adc_set_clkdiv(divisor_clock_adc);

    capturar_amostras_via_dma(buffer_de_amostras, total_de_amostras);

    // Aplica o filtro passa-baixa para suavizar o sinal
    uint16_t amostra_suavizada = buffer_de_amostras[0];
    for (size_t i = 1; i < total_de_amostras; ++i) {
        amostra_suavizada = suavizar_sinal_audio(buffer_de_amostras[i], amostra_suavizada, FATOR_SUAVIZACAO);
        buffer_de_amostras[i] = amostra_suavizada;
    }

    return total_de_amostras;
}

void processo_de_reproducao(uint pino_a, uint pino_b, uint16_t *dados, size_t n_amostras, uint32_t freq_amostragem) {
    uint32_t clock = clock_get_hz(clk_sys);
    uint32_t valor_max_pwm = clock / freq_amostragem - 1;
    if (valor_max_pwm == 0) valor_max_pwm = 1;

    for (size_t i = 0; i < n_amostras; ++i) {
        // Amplifica o sinal e garante que não ultrapasse o limite de 12-bit (4095)
        uint32_t amostra_amplificada = (uint32_t)(dados[i] * GANHO_SAIDA_AUDIO);
        if (amostra_amplificada > 4095) amostra_amplificada = 4095;
        
        // Converte o valor da amostra (0-4095) para o nível do PWM
        uint32_t nivel_pwm = (amostra_amplificada * valor_max_pwm) / 4095;
        
        pwm_set_gpio_level(pino_a, nivel_pwm);
        pwm_set_gpio_level(pino_b, nivel_pwm);
        
        // Aguarda o tempo exato entre as amostras
        sleep_us(1000000 / freq_amostragem);
    }
    // Desliga os buzzers ao final
    pwm_set_gpio_level(pino_a, 0);
    pwm_set_gpio_level(pino_b, 0);
}

// ----------------------------------------
// --- Funções de Apoio e Utilitários ---
// ----------------------------------------

void definir_cor_led(bool vermelho, bool verde, bool azul) {
    gpio_put(PINO_LED_VERMELHO, vermelho);
    gpio_put(PINO_LED_VERDE, verde);
    gpio_put(PINO_LED_AZUL, azul);
}

void tratador_interrupcao_botao(uint pino, uint32_t eventos) {
    uint32_t agora = to_ms_since_boot(get_absolute_time());

    if (pino == PINO_BOTAO_GRAVAR) {
        if (agora - ultimo_acionamento_gravar > TEMPO_DEBOUNCE_BOTAO_MS) {
            flag_botao_gravar_ativado = true;
            ultimo_acionamento_gravar = agora;
        }
    }
    if (pino == PINO_BOTAO_REPRODUZIR) {
        if (agora - ultimo_acionamento_reproduzir > TEMPO_DEBOUNCE_BOTAO_MS) {
            flag_botao_reproduzir_ativado = true;
            ultimo_acionamento_reproduzir = agora;
        }
    }
}

void capturar_amostras_via_dma(uint16_t *destino, size_t n_amostras) {
    adc_fifo_drain();
    adc_run(false);

    dma_channel_configure(canal_dma_adc, &config_canal_dma, destino, &(adc_hw->fifo), n_amostras, true);

    adc_run(true);
    dma_channel_wait_for_finish_blocking(canal_dma_adc);
    adc_run(false);
}

uint16_t suavizar_sinal_audio(uint16_t amostra_atual, uint16_t amostra_anterior, float fator) {
    // Filtro IIR simples (média móvel exponencial)
    return (uint16_t)(fator * amostra_atual + (1.0f - fator) * amostra_anterior);
}

void mostrar_waveform_display(uint8_t *buffer_tela, uint16_t *dados_audio, size_t n_amostras) {
    memset(buffer_tela, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT / 8); // Limpa o buffer
    ssd1306_draw_string(buffer_tela, 10, 0, "Onda capturada"); // Título Alterado

    // Define a área de desenho e o centro vertical
    int y_offset = 12; // Espaço para o título
    int altura_desenho = DISPLAY_HEIGHT - y_offset;
    int y_centro = y_offset + (altura_desenho / 2);

    const int ADC_ZERO = 2048; 
    const float GANHO_VISUAL = 4.0f; // Amplifica a onda 4x no display

    size_t colunas_a_desenhar = (n_amostras < DISPLAY_WIDTH) ? n_amostras : DISPLAY_WIDTH;
    for (size_t x = 0; x < colunas_a_desenhar; x++) {
        // Reduz a amostragem para caber na tela se necessário
        size_t indice_amostra = (n_amostras > DISPLAY_WIDTH) ? (x * n_amostras / DISPLAY_WIDTH) : x;
        
        // Calcula a amplitude relativa ao centro (-2047 a +2047)
        int amplitude = (int)dados_audio[indice_amostra] - ADC_ZERO;

        // Mapeia a amplitude para a altura do display, aplicando o ganho visual
        int y_final = y_centro - (int)((float)amplitude / ADC_ZERO * (altura_desenho / 2.0f) * GANHO_VISUAL);

        // Garante que o desenho não saia da área útil (clipping)
        if (y_final < y_offset) y_final = y_offset;
        if (y_final >= DISPLAY_HEIGHT) y_final = DISPLAY_HEIGHT - 1;

        // Desenha a linha vertical do centro até a amplitude
        if (y_final > y_centro) { // Amplitude para baixo
            for (int y = y_centro; y <= y_final; y++) {
                ssd1306_set_pixel(buffer_tela, x, y, true);
            }
        } else { // Amplitude para cima
            for (int y = y_final; y <= y_centro; y++) {
                ssd1306_set_pixel(buffer_tela, x, y, true);
            }
        }
    }

    // Envia o buffer desenhado para o display
    struct render_area area_render = {
        .start_column = 0, .end_column = ssd1306_width - 1,
        .start_page = 0, .end_page = ssd1306_n_pages - 1
    };
    calculate_render_area_buffer_length(&area_render);
    render_on_display(buffer_tela, &area_render);
}

void apagar_tela() {
    memset(frame_buffer_display, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT / 8);
    struct render_area area_render = {
        .start_column = 0, .end_column = ssd1306_width - 1,
        .start_page = 0, .end_page = ssd1306_n_pages - 1
    };
    calculate_render_area_buffer_length(&area_render);
    render_on_display(frame_buffer_display, &area_render);
}