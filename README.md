# Sintetizador de Áudio com Raspberry Pi Pico

**Autor:** Gabriel da Conceição Miranda  
**Data:** 07 de junho de 2025  
**Placa-alvo:** BitDogLab / Raspberry Pi Pico

---

## 📄 Descrição

Este projeto é um **sintetizador de áudio simples**, desenvolvido como parte do Projeto 2 do curso. O sistema é capaz de:

- Gravar um trecho de áudio de duração pré-definida através de um microfone.
- Armazenar esse áudio em um buffer digital.
- Reproduzi-lo utilizando buzzers passivos.

O projeto foi implementado em **C estruturado** para o microcontrolador **Raspberry Pi Pico**, utilizando o **SDK oficial**. Ele explora conceitos fundamentais de eletrônica e sistemas embarcados, como:

- Conversão Analógico-Digital (ADC)  
- Modulação por Largura de Pulso (PWM)  
- Uso de DMA para captura eficiente de dados  

---

## ⚙️ Funcionalidades Principais

- **Gravação e Reprodução**  
  Controladas por dois botões dedicados: um para iniciar a gravação e outro para a reprodução.

- **Feedback Visual com LED RGB**  
  - 🔴 **Vermelho:** Sistema está gravando.  
  - 🟢 **Verde:** Áudio está sendo reproduzido.

- **Visualização da Forma de Onda**  
  Exibição gráfica no display OLED após a gravação.

- **Processamento de Sinal**  
  Aplicação de filtro digital passa-baixas para suavização e remoção de ruídos.

- **Controle Robusto de Botões**  
  Debounce por software para evitar múltiplos acionamentos indesejados.

---

## 🔧 Hardware Necessário

- Placa **Raspberry Pi Pico** ou compatível (ex: BitDogLab)
- **Microfone** (conectado ao GP28 / ADC2)
- **Display OLED 128x64** com controlador SSD1306 (I2C nos pinos GP14 e GP15)
- **LED RGB** (pinos GP13, GP11, GP12)
- **2 Botões de pressão** (GP5 e GP6)
- **2 Buzzers passivos** ou **alto-falante com amplificador** (GP21 e GP10)

---

## 🧪 Configuração e Compilação

1. **Pré-requisitos**  
   - Raspberry Pi Pico C/C++ SDK  
   - CMake  
   - Ninja  
   - Compilador ARM  
   - VS Code com extensão *Pico-W-Go* (ou similar)

2. **Clone o Repositório**  
   ```bash
   git clone https://github.com/EmbarcaTech-2025/projeto-orientado-2-sintetizador-de-udio-MirandaBiel.git
