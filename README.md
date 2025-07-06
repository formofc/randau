# **RANDom AUdio generator**  

## What? Why? Who?  
- **What**: CLI tool that generates procedural audio nightmares  
- **Why**: 3AM coding sessions
- **Who**: Me 

## Build & Run  
```bash  
cc src/main.c -I includes -lm -o randau  

./randau -c 20 -f 50 5000 -b 5 30 -a 1 1 -v 1

./randau -S hell.wav -d 0.25 -C 240 -f 20 120 -c 10  
```

## Usage
```
Usage: randau [options]
Options:
  -h, --help                   Show this help message
  -r, --sample-rate R          Set sample rate (default: 44100)
  -d, --duration D             Set duration in seconds (default: 7.000000)
  -c, --oscillators C          Set number of oscillators (default: 7, maximum: 128)
  -C, --loop-count L           Set loop count for recording (only with -S)
  -s, --save FILE              Save while playing (FILE is output .wav)
  -S, --save-only FILE         Save without playing (FILE is output .wav)
  -v, --volume V               Set master volume (default: 0.150000)
  -f, --freq-range N X         Set frequency range (default: [110.000000, 440.000000])
  -a, --amplitude-range N X    Set amplitude range (default: [0.100000, 1.000000])
  -b, --bps-range N X          Set beat per second range (default: [0.100000, 10.000000])
```

## Features  
- 6 oscillator types: `FLAT` `SAWTOOTH` `NOISE` `BEAT` `PULSE` `WAVE`  
- Live audio generation / WAV recording  
- Random over your control: randomize params with options  

## License  
[MIT](LICENSE)  
