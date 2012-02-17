library IEEE;
        use IEEE.std_logic_1164.all;
        use IEEE.std_logic_unsigned.all;
library work;
        use work.camera.all ;

entity camera_interface is
	port(
 		clock : in std_logic; 
 		i2c_clk : in std_logic; 
 		arazb : in std_logic; 
 		pixel_data : in std_logic_vector(7 downto 0 ); 
 		y_data : inout std_logic_vector(7 downto 0 ); 
 		u_data : inout std_logic_vector(7 downto 0 ); 
 		v_data : inout std_logic_vector(7 downto 0 ); 
 		scl : inout std_logic; 
 		sda : inout std_logic; 
 		new_pix, new_line, new_frame : out std_logic; 
 		pxclk, href, vsync : in std_logic
	); 
end camera_interface;

architecture systemc of camera_interface is
	constant NB_REGS : integer := 255; 
	constant OV7670_I2C_ADDR : std_logic_vector(6 downto 0) := "1000010"; 
	TYPE pixel_state IS (Y1, U1, Y2, V1) ; 
	TYPE registers_state IS (INIT, SEND_ADDR, SEND_DATA, NEXT_REG, STOP) ; 
	signal i2c_data : std_logic_vector(7 downto 0 ) ; 
	signal reg_data : std_logic_vector(15 downto 0 ) ; 
	signal i2c_addr : std_logic_vector(6 downto 0 ) ; 
	signal send : std_logic ; 
	signal rcv : std_logic ; 
	signal dispo : std_logic ; 
	signal ack_byte : std_logic ; 
	signal pix_state : pixel_state ; 
	signal next_state : pixel_state ; 
	signal reg_state : registers_state ; 
	signal y1_delay, u_delay, v_delay, y2_delay : std_logic_vector(7 downto 0 ) ; 
	signal sccb_wr, sccb_rd : std_logic ; 
	signal valid_pixel : std_logic ; 
	signal reg_addr : std_logic_vector(7 downto 0 ) ;
	begin
	
	register_rom0 : register_rom
		port map ( 
			addr => reg_addr, 
			data => reg_data
		); 
	i2c_master0 : i2c_master
		port map ( 
			clock => i2c_clk, 
			arazb => arazb, 
			sda => sda, 
			scl => scl, 
			data => i2c_data, 
			slave_addr => i2c_addr, 
			send => send, 
			rcv => rcv, 
			dispo => dispo, 
			ack_byte => ack_byte
		); 
	
	-- sccb_interface
	process(dispo, ack_byte, arazb)
		 begin
		 	i2c_addr <= OV7670_I2C_ADDR ;
		 	if  NOT arazb = '1'  then
		 		reg_state <= init ;
		 	else
		 		case reg_state is
		 			when init => 
		 				if  dispo = '1'  then
		 					send <= '1' ; 
		 					i2c_data <= reg_data(15 downto 8) ; 
		 					reg_state <= send_addr ;
		 				end if ;
		 			when send_addr => 
		 				if  ack_byte = '1'  then
		 					send <= '1' ; 
		 					i2c_data <= reg_data(7 downto 0) ; 
		 					reg_state <= send_data ;
		 				end if ;
		 			when send_data => 
		 				if  ack_byte = '1'  then
		 					send <= '0' ; 
		 					reg_state <= next_reg ; 
		 					reg_addr <= (reg_addr + 1) ;
		 				end if ;
		 			when next_reg => 
		 				send <= '0' ;
		 				if ( NOT ack_byte = '1' ) AND  reg_data /= X"FFFF"  AND  dispo = '1'  AND  conv_integer(reg_addr) < 255  then
		 					reg_state <= send_addr ; 
		 					i2c_data <= reg_data(15 downto 8) ; 
		 					send <= '1' ;
		 				elsif  conv_integer(reg_addr) >= 255  OR  reg_data = X"FFFF"  then
		 					reg_state <= stop ;
		 				end if ;
		 			when stop => 
		 				send <= '0' ;
		 				reg_state <= stop ;
		 			when others => 
		 				reg_state <= init ;
		 		end case ;
		 	end if ;
		 end process;  

	-- pixel_interface
	process(clock, arazb)
		 begin
		 	new_line <= NOT href ;
		 	new_frame <= vsync ;
		 	if  NOT arazb = '1'  then
		 		pix_state <= Y1 ;
		 	elsif  clock'event and clock = '1'  then
		 		if  pxclk = '1'  AND  href = '1'  AND  NOT vsync = '1'  then
		 			case pix_state is
		 				when Y1 => 
		 					y2_delay <= pixel_data ;
		 					new_pix <= valid_pixel ;
		 					next_state <= U1 ;
		 				when U1 => 
		 					y_data <= y1_delay ;
		 					u_data <= u_delay ;
		 					v_data <= v_delay ;
		 					u_delay <= pixel_data ;
		 					new_pix <= '0' ;
		 					next_state <= Y2 ;
		 				when Y2 => 
		 					y1_delay <= pixel_data ;
		 					new_pix <= valid_pixel ;
		 					next_state <= V1 ;
		 				when V1 => 
		 					y_data <= y2_delay ;
		 					u_data <= u_delay ;
		 					v_data <= pixel_data ;
		 					v_delay <= pixel_data ;
		 					new_pix <= '0' ;
		 					valid_pixel <= '1' ;
		 					next_state <= Y1 ;
		 				when others => 
		 					next_state <= Y1 ;
		 			end case ;
		 		elsif ( NOT pxclk = '1' ) AND  href = '1'  AND  NOT vsync = '1'  then
		 			pix_state <= next_state ;
		 		else
		 			valid_pixel <= '0' ;
		 		end if ;
		 	end if ;
		 end process;  
	
end systemc ;