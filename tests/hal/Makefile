# Tests hôte des modules HAL purs (sans Arduino).
CXX      = g++
CXXFLAGS = -O2 -Wall -Wextra -I../../src/hal
HAL      = ../../src/hal
TESTS    = test_serial_proto test_disk_select test_vga_pack test_zorro

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

test_serial_proto: test_serial_proto.cpp $(HAL)/serial_proto.cpp $(HAL)/serial_proto.h
	$(CXX) $(CXXFLAGS) -o $@ test_serial_proto.cpp $(HAL)/serial_proto.cpp

test_disk_select: test_disk_select.cpp $(HAL)/disk_select.cpp $(HAL)/disk_select.h
	$(CXX) $(CXXFLAGS) -o $@ test_disk_select.cpp $(HAL)/disk_select.cpp

test_vga_pack: test_vga_pack.cpp $(HAL)/vga_pack.h
	$(CXX) $(CXXFLAGS) -o $@ test_vga_pack.cpp

test_zorro: test_zorro.cpp $(HAL)/zorro.cpp $(HAL)/zorro.h
	$(CXX) $(CXXFLAGS) -o $@ test_zorro.cpp $(HAL)/zorro.cpp

clean:
	rm -f $(TESTS)

.PHONY: test clean
