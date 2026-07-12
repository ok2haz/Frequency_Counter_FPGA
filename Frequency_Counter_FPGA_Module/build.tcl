open_project "C:/GitHub/Frequency_Counter_FPGA/Frequency_Counter_FPGA_Module/Counter_FPGA.gprj"
# Piny 54-57 jsou dedikované SSPI/MSPI konfigurační piny -> povol jako běžné GPIO
set_option -use_sspi_as_gpio 1
set_option -use_mspi_as_gpio 1
# Vyssi usili placeru/routeru: cross-fazove cesty TDC (2,5/5/7,5 ns rozpocty)
set_option -place_option 1
set_option -route_option 1
run all
