----------------------------------------------------------------------------------
-- Company:LAAS-CNRS 
-- Author:Jonathan Piat <piat.jonathan@gmail.com> 
-- 
-- Create Date:    09:45:03 06/19/2012 
-- Design Name: 
-- Module Name:    muxed_addr_interface - Behavioral 
-- Project Name: 
-- Target Devices: Spartan 6 
-- Tool versions: ISE 14.1 
-- Description: 
--
-- Dependencies: 
--
-- Revision: 
-- Revision 0.01 - File Created
-- Additional Comments: 
--
----------------------------------------------------------------------------------
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;

-- Uncomment the following library declaration if using
-- arithmetic functions with Signed or Unsigned values
--use IEEE.NUMERIC_STD.ALL;

library work ;
use work.utils_pack.all ;

-- Uncomment the following library declaration if instantiating
-- any Xilinx primitives in this code.
--library UNISIM;
--use UNISIM.VComponents.all;

entity muxed_addr_interface is
generic(ADDR_WIDTH : positive := 8 ; DATA_WIDTH : positive := 16);
port(clk, resetn : in std_logic ;
	  data	:	inout	std_logic_vector((DATA_WIDTH - 1) downto 0);
	  wrn, oen, addr_en_n, csn : in std_logic ;
	  be0n, be1n : in std_logic ;
	  data_bus_out	: out	std_logic_vector((DATA_WIDTH - 1) downto 0);
	  data_bus_in	: in	std_logic_vector((DATA_WIDTH - 1) downto 0);
	  addr_bus	:	out	std_logic_vector((ADDR_WIDTH - 1) downto 0);
	  wr, rd	:	out	std_logic
);
end muxed_addr_interface;


architecture Behavioral of muxed_addr_interface is
signal latch_addr, wrt, rdt, cst : std_logic ;
signal data_bus_out_t	: std_logic_vector((DATA_WIDTH - 1) downto 0);
begin

latch_addr <= '1' when csn = '0' and addr_en_n = '0' and wrn = '1' and oen = '1' else
				   '0' ;
					
add_latch0 : generic_latch 
	 generic map(NBIT => ADDR_WIDTH)
    Port map( clk => clk ,
           resetn => resetn ,
           sraz => '0' ,
           en => latch_addr,
           d => data((ADDR_WIDTH - 1) downto 0),
           q => addr_bus);

process(clk, resetn)
begin
if resetn ='0' then
	wrt <= '0' ;
	rdt <= '0' ;
	data_bus_out_t <= (others => 'Z');
elsif clk'event and clk ='1' then
	wrt <= (NOT wrn) and (NOT csn) and (NOT latch_addr) and (be0n); -- only write on  high bytes
	rdt <= (NOT oen) and (NOT csn)  and (NOT latch_addr) ;
	if latch_addr = '0' and wrn = '0' and csn = '0' and be0n = '0' and be1n = '1' then
		data_bus_out_t(7 downto 0) <= data(7 downto 0) ;
	elsif latch_addr = '0' and wrn = '0' and csn = '0' and be0n = '1' and be1n = '0' then
		data_bus_out_t(15 downto 8) <= data(15 downto 8) ;
	elsif latch_addr = '0' and wrn = '0' and csn = '0' and be0n = '1' and be1n = '1' then
		data_bus_out_t <= data ;
	end if;
end if ;
end process;

wr <= wrt ;
rd <= rdt ;

data <= data_bus_in when (oen = '0' and csn = '0') else
		  (others => 'Z');


data_bus_out <= data_bus_out_t ;


end Behavioral;




architecture RTL of muxed_addr_interface is
signal latch_addr, wrt, rdt, cst : std_logic ;
signal data_bus_out_t	: std_logic_vector((DATA_WIDTH - 1) downto 0);
begin

latch_addr <= '1' when csn = '0' and addr_en_n = '0' and wrn = '1' and oen = '1' else
				   '0' ;
					
add_latch0 : generic_latch 
	 generic map(NBIT => ADDR_WIDTH)
    Port map( clk => clk ,
           resetn => resetn ,
           sraz => '0' ,
           en => latch_addr,
           d => data((ADDR_WIDTH - 1) downto 0),
           q => addr_bus);

process(clk, resetn)
begin
if resetn ='0' then
	wrt <= '0' ;
	rdt <= '0' ;
	data_bus_out_t <= (others => 'Z');
elsif clk'event and clk ='1' then
	wrt <= (NOT wrn) and (NOT csn) and (NOT latch_addr) ;
	rdt <= (NOT oen) and (NOT csn)  and (NOT latch_addr) ;
	if latch_addr = '0' and wrn = '0' and csn = '0' then
		data_bus_out_t <= data ;
	end if ;
end if ;
end process;

wr <= wrt ;
rd <= rdt ;

data <= data_bus_in when (oen = '0' and csn = '0') else
		  (others => 'Z');


-- byte access might kill the fifos !
data_bus_out <= data_bus_out_t when be0n = '1' and be1n = '1' else
					 data_bus_out_t when be0n = '0' and be1n = '0' else
					 (data_bus_out_t(15 downto 8) & data_bus_in(7 downto 0)) when be1n = '0' else
					 (data_bus_in(15 downto 8) & data_bus_out_t(7 downto 0));


end RTL ;

