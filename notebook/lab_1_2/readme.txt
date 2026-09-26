lab 1_2

no pico needed for this part.
course c files stay in lab_1_2/. this folder is the pc copy:
quantization 16/8/4, stats, fft. same math as the .c files.

synthetic imu until you have a lab_1_1 .bin.

from /media/sf_repo (or this folder on the mac):

  python3 notebook/lab_1_2/run.py
  python3 notebook/lab_1_2/run.py --bin path/to/daq_000.bin

needs numpy+matplotlib for the overlay plots. system python may not have them.
the phd venv does:

  /Users/joosep/IdeaProjects/joosep/phd/venv/bin/python3 notebook/lab_1_2/run.py

writes notebook/lab_1_2/out/
  quant_summary.csv   snr/rmse at 16/8/4
  stats.csv           mean/median/var per axis
  raw_imu_mcu.txt     time block (course fft.py can read this)
  out_fft_mcu.txt     top peaks
  imu_signal.png
  imu_fft_spectrum.png

later, with the board: run the course .c on the pico, dump the same
txt files off the sd card, overlay. not wired yet.
