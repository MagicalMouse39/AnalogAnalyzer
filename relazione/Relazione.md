---
title: Campionatore e analizzatore analogico
author: Antonio Sgalla
date: 24/05/2023
---
# Campionatore e analizzatore analogico

## Descrizione del progetto
Realizzare un campionatore di un segnale analogico a `8 bit`, alla massima velocità consentita dal processore, che visualizzi graficamente i valori letti su una matrice di led `8x8` tramite interfaccia `SPI MAX7219`.

Il sistema deve consentire di impostare la base dei tempi e quindi visualizzare, per ogni colonna di led, il valor medio progressivo dei valori acquisiti in ingresso.

Ad esempio,  se il tempo di conversione ADC è `40 us`, impostando una base dei tempi a `60 uS/div`, ogni valore visualizzato sarà la media progressiva di tre campioni `((a0+a1)/2+a2)/2`

## Schema del circuito
![Schema elettrico realizzato con Fritzing](ProgettoFritzing.PNG "Schema elettrico realizzato con Fritzing")

## Diagramma degli stati
![Diagramma degli stati](diagrammaStati.PNG "Diagramma degli stati")

## Flowchart
(Solo funzioni principali, per il flowchart completo allego il file `.fprg`)

### Funzione setup
![Funzione setup](setup.PNG "Funzione setup")

### Funzione loop
![Funzione loop](loop.PNG "Funzione loop")

### Funzione startRecording
![Funzione start recording](startRec.PNG "Funzione start recording")

### Funzione stopRecording
![Funzione stop recording](stopRec.PNG "Funzione stop recording")

### Funzione di gestione dell'interrupt ADC
![Funzione di gestione interrupt ADC](intADC.PNG "Funzione di gestione interrupt ADC")

## Codice arduino
(Solo funzioni principali, per il codice completo allego il file `.ino`)

### Definizione delle macro
```arduino
#define _NBV ~_BV

#define sbi(reg, bit) (reg |= _BV(bit))
#define cbi(reg, bit) (reg &= _NBV(bit))

#define cli() __asm__ __volatile__ ("cli");
#define sei() __asm__ __volatile__ ("sei");
```

### Funzione setup
```arduino
void setup() {
  // Imposto tutti i pin da 0 a 7 in OUTPUT
  DDRD = 0xFF;

  // Inizializzo la variabile di stato
  isRecording = false;

  // Inizializzo la base dei tempi a 0
  timeBase = 0;

  // Imposto solamente il pin 7 (bottone) come input
  cbi(DDRD, DDD7);

  // Inizializzo la matrice led
  ledInit();
}
```

### Funzione loop
```arduino
void loop() {
  // Variabile d'appoggio: true se il pulsante viene premuto in quell'istante
  bool startBtnDown = PIND & _BV(DDD7);

  // Meccanismo SET-RESET per chiamare solo una volta la funzione anche se il pulsante viene tenuto premuto
  if (startBtnDown && !lockStartBtn) {
    // SET
    lockStartBtn = true;
    
    if (isRecording) {
      // Fermo la registrazione dei dati
      stopRecording();
    } else {
      // Inizio della registrazione dei dati
      startRecording();
    }
  } else if (!startBtnDown) {
    // RESET
    lockStartBtn = false;
  }
  
  // Quando finisco lo spazio per i campioni, ritorno all'inizio dell'array di campionamento, creando un array circolare
  if (dataIndex >= MAX_REC_LEN) {
    dataIndex = 0;
  }

  if (isRecording) {    
    for (byte i = 0; i < 8; i++) {
      ledSetCol(i, byteToLeds(progrAvg(data, dataIndex - i - 3, 3)));
    }
  } else {
    timeBase = readAnalogByte(A5);
    byte row = byteToLeds(timeBase);
    ledSetRow(3, row);
    ledSetRow(4, row);
  }
}
```

### Funzione startRecording
```arduino
void startRecording() {
  // Imposto la variabile di stato
  isRecording = true;

  // Azzero l'indice dell'array
  dataIndex = 0;

  // Accendo il led
  sbi(PORTD, DDD2);

  // Disabilito gli interrupt utilizzando la funzione da me definita (non tanto diversa da quella di default)
  cli();

  // Cancello ADLAR (5) in ADMUX per allineare a destra il risultato
  // Gli 8 bit inferiori andranno in ADCL, mentre i 2 superiori andranno in ADCH
  cbi(ADMUX, ADLAR);
 
  // Metto REFS0 (6) a 1 in ADMUX per impostare la sorgente giusta (01)
  sbi(ADMUX, REFS0);
 
  // Cancello da MUX3 a MUX0 in ADMUX in preparazione all'assegnazioe del pin analogico
  cbi(ADMUX, MUX3);
  cbi(ADMUX, MUX2);
  cbi(ADMUX, MUX1);
  cbi(ADMUX, MUX0);
 
  // Imposto il pin analogico da controllare (il 5 in questo caso)
  // L'AND serve per essere sicuri di non superare il limite di 4 bit (0xF = 15 = 0b1111)
  ADMUX |= 4 & 0xF;
 
  // Imposto ADEN (7) in ADCSRA per abilitare l'ADC.
  // N.B. per eseguire questa istruzione servono 12 cicli di clock ADC
  sbi(ADCSRA, ADEN);

  // Imposto ADATE (5) in ADCSRA per abilitare l'auto-triggering.
  sbi(ADCSRA, ADATE);

  // Cancello da ADTS2 a ADTS0 in ADCSRB per impostare la modalità di trigger a "free running".
  // Significa che appena una conversione ADC termina, ne verrà fatta una nuova
  cbi(ADCSRB, ADTS2);
  cbi(ADCSRB, ADTS1);
  cbi(ADCSRB, ADTS0);
 
  // Imposto il prescaler a 128 (16000KHz/128 = 125KHz)
  // Sopra i 200KHz i risultati non sono affidabili.
  // Tempo di conversione: 1/125000 * 13
  sbi(ADCSRA, CS10);
  sbi(ADCSRA, CS11);
  sbi(ADCSRA, CS12);
 
  // Imposto ADIE in ADCSRA per abilitare l'interrupt ADC.
  sbi(ADCSRA, ADIE);

  // Abilito gli interrupt utilizzando la funzione da me definita
  sei();

  // Imposto ADSC in ADCSRA per iniziare la conversione ADC (la prima volta è necessario, poi continua automaticamente fino all'impostazione di ADSC a 0)
  sbi(ADCSRA, ADSC);
}
```

### Funzione stopRecording
```arduino
void stopRecording() {
  // Spengo il led
  cbi(PORTD, DDD2);

  // Cancello ADSC (6) da ADCSRA per fermare la conversione analogico-digitale
  cbi(ADCSRA, ADSC);
  // Cancello ADATE (5) da ADCSRA per disabilitare l'auto-triggering.
  cbi(ADCSRA, ADATE);
  // Cancello ADIE (3) da ADCSRA per disabilitare l'interrupt ADC.
  cbi(ADCSRA, ADIE);

  // Pulisco la matrice led
  ledClear();

  // Azzero l'indice dell'array
  dataIndex = 0;

  // Imposto la variabile di stato
  isRecording = false;
}
```

### Funzione di gestione dell'interrupt ADC
```arduino
ISR(ADC_vect){
  // Bisogna leggere ADCL per primo
  data[dataIndex++] = (ADCL | (ADCH << 8)) >> 2;
}
```

### Disassembly della funzione di gestionre dell'interrupt ADC
Il simbolo della funzione è `__vector_21` perché `21` è la posizione dell'interrupt `ADC` nell'array degli interrupt

Totale cicli di clock: `78`

```asm
Cicli di clock    Bytes         Opcode  Parametri

00000238 <__vector_21>:
 2:               1f 92         push    r1
 2:               0f 92         push    r0
 1:               0f b6         in      r0, 0x3f        ; 63
 2:               0f 92         push    r0
 1:               11 24         eor     r1, r1
 2:               2f 93         push    r18
 2:               3f 93         push    r19
 2:               4f 93         push    r20
 2:               5f 93         push    r21
 2:               8f 93         push    r24
 2:               9f 93         push    r25
 2:               ef 93         push    r30
 2:               ff 93         push    r31
 2:               90 91 78 00   lds     r25, 0x0078     ; 0x800078 <__DATA_REGION_ORIGIN__+0x18>
 2:               80 91 79 00   lds     r24, 0x0079     ; 0x800079 <__DATA_REGION_ORIGIN__+0x19>
 2:               20 91 40 05   lds     r18, 0x0540     ; 0x800540 <dataIndex>
 2:               30 91 41 05   lds     r19, 0x0541     ; 0x800541 <dataIndex+0x1>
 1:               a9 01         movw    r20, r18
 1:               4f 5f         subi    r20, 0xFF       ; 255
 1:               5f 4f         sbci    r21, 0xFF       ; 255
 2:               50 93 41 05   sts     0x0541, r21     ; 0x800541 <dataIndex+0x1>
 2:               40 93 40 05   sts     0x0540, r20     ; 0x800540 <dataIndex>
 1:               20 5c         subi    r18, 0xC0       ; 192
 1:               3e 4f         sbci    r19, 0xFE       ; 254
 1:               89 27         eor     r24, r25
 1:               98 27         eor     r25, r24
 1:               89 27         eor     r24, r25
 1:               95 95         asr     r25
 1:               87 95         ror     r24
 1:               95 95         asr     r25
 1:               87 95         ror     r24
 1:               f9 01         movw    r30, r18
 2:               80 83         st      Z, r24
 2:               ff 91         pop     r31
 2:               ef 91         pop     r30
 2:               9f 91         pop     r25
 2:               8f 91         pop     r24
 2:               5f 91         pop     r21
 2:               4f 91         pop     r20
 2:               3f 91         pop     r19
 2:               2f 91         pop     r18
 2:               0f 90         pop     r0
 1:               0f be         out     0x3f, r0        ; 63
 2:               0f 90         pop     r0
 2:               1f 90         pop     r1
 4:               18 95         reti
```

## Fotografie progetto fisico

### Prototipo spento
![Prototipo spento](projOff.jpg "Prototipo spento")

### Prototipo acceso, in attesa di base dei tempi
![Prototipo acceso, in attesa di base dei tempi](projOnIdle.jpg "Prototipo acceso, in attesa di base dei tempi")

### Prototipo acceso, in registrazione
![Prototipo acceso, in registrazione](projOnRecording.jpg "Prototipo acceso, in registrazione")