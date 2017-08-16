// https://www.shadertoy.com/view/XsXGDX

// Median filter via sorting networks. Was mostly just curious to see if I could break WebGL with sorting networks.  It's a pretty dodgy way to do median.

// iChannel0: Source, filter=mipmap, wrap=repeat
// BBox: iChannel0

// max is SORT_SIZE/2
uniform int radius = 1; // Radius, min=0, max=5

//Looks like 16 is too much for Windows!
//16/32 is pretty good on Mac, 64 works but slow.
#define SORT_SIZE	8

//get color by packing 3 channels into 1 Or just do luminance to get some fps back :)
#define COLOR 1	

//ramp the radius up and down... at the cost of some silly code which goes also slower
#define RAMP 1


float sort[SORT_SIZE];

#define SWAP(a,b) { float t = max(sort[a],sort[b]); sort[a] = min(sort[a],sort[b]); sort[b] = t; }

//various sized sorting networks generated with this:
//http://pages.ripco.net/~jgamble/nw.html

#define SORT8 SWAP(0, 1);  SWAP(2, 3); SWAP(0, 2); SWAP(1, 3); SWAP(1, 2); SWAP(4, 5); SWAP(6, 7); SWAP(4, 6); SWAP(5, 7); SWAP(5, 6); SWAP(0, 4); SWAP(1, 5); SWAP(1, 4); SWAP(2, 6); SWAP(3, 7); SWAP(3, 6); SWAP(2, 4); SWAP(3, 5); SWAP(3, 4);

//There are 60 comparators in this network, grouped into 10 parallel operations.
#define SORT16 SWAP(0, 1); SWAP(2, 3); SWAP(4, 5); SWAP(6, 7); SWAP(8, 9); SWAP(10, 11); SWAP(12, 13); SWAP(14, 15); SWAP(0, 2); SWAP(4, 6); SWAP(8, 10); SWAP(12, 14); SWAP(1, 3); SWAP(5, 7); SWAP(9, 11); SWAP(13, 15); SWAP(0, 4); SWAP(8, 12); SWAP(1, 5); SWAP(9, 13); SWAP(2, 6); SWAP(10, 14); SWAP(3, 7); SWAP(11, 15); SWAP(0, 8); SWAP(1, 9); SWAP(2, 10); SWAP(3, 11); SWAP(4, 12); SWAP(5, 13); SWAP(6, 14); SWAP(7, 15); SWAP(5, 10); SWAP(6, 9); SWAP(3, 12); SWAP(13, 14); SWAP(7, 11); SWAP(1, 2); SWAP(4, 8); SWAP(1, 4); SWAP(7, 13); SWAP(2, 8); SWAP(11, 14); SWAP(2, 4); SWAP(5, 6); SWAP(9, 10); SWAP(11, 13); SWAP(3, 8); SWAP(7, 12); SWAP(6, 8); SWAP(10, 12); SWAP(3, 5); SWAP(7, 9); SWAP(3, 4); SWAP(5, 6); SWAP(7, 8); SWAP(9, 10); SWAP(11, 12); SWAP(6, 7); SWAP(8, 9);

//There are 191 comparators in this network, grouped into 15 parallel operations.
#define SORT32 SWAP(0, 16); SWAP(1, 17); SWAP(2, 18); SWAP(3, 19); SWAP(4, 20); SWAP(5, 21); SWAP(6, 22); SWAP(7, 23); SWAP(8, 24); SWAP(9, 25); SWAP(10, 26); SWAP(11, 27); SWAP(12, 28); SWAP(13, 29); SWAP(14, 30); SWAP(15, 31); SWAP(0, 8); SWAP(1, 9); SWAP(2, 10); SWAP(3, 11); SWAP(4, 12); SWAP(5, 13); SWAP(6, 14); SWAP(7, 15); SWAP(16, 24); SWAP(17, 25); SWAP(18, 26); SWAP(19, 27); SWAP(20, 28); SWAP(21, 29); SWAP(22, 30); SWAP(23, 31); SWAP(8, 16); SWAP(9, 17); SWAP(10, 18); SWAP(11, 19); SWAP(12, 20); SWAP(13, 21); SWAP(14, 22); SWAP(15, 23); SWAP(0, 4); SWAP(1, 5); SWAP(2, 6); SWAP(3, 7); SWAP(24, 28); SWAP(25, 29); SWAP(26, 30); SWAP(27, 31); SWAP(8, 12); SWAP(9, 13); SWAP(10, 14); SWAP(11, 15); SWAP(16, 20); SWAP(17, 21); SWAP(18, 22); SWAP(19, 23); SWAP(0, 2); SWAP(1, 3); SWAP(28, 30); SWAP(29, 31); SWAP(4, 16); SWAP(5, 17); SWAP(6, 18); SWAP(7, 19); SWAP(12, 24); SWAP(13, 25); SWAP(14, 26); SWAP(15, 27); SWAP(0, 1); SWAP(30, 31); SWAP(4, 8); SWAP(5, 9); SWAP(6, 10); SWAP(7, 11); SWAP(12, 16); SWAP(13, 17); SWAP(14, 18); SWAP(15, 19); SWAP(20, 24); SWAP(21, 25); SWAP(22, 26); SWAP(23, 27); SWAP(4, 6); SWAP(5, 7); SWAP(8, 10); SWAP(9, 11); SWAP(12, 14); SWAP(13, 15); SWAP(16, 18); SWAP(17, 19); SWAP(20, 22); SWAP(21, 23); SWAP(24, 26); SWAP(25, 27); SWAP(2, 16); SWAP(3, 17); SWAP(6, 20); SWAP(7, 21); SWAP(10, 24); SWAP(11, 25); SWAP(14, 28); SWAP(15, 29); SWAP(2, 8); SWAP(3, 9); SWAP(6, 12); SWAP(7, 13); SWAP(10, 16); SWAP(11, 17); SWAP(14, 20); SWAP(15, 21); SWAP(18, 24); SWAP(19, 25); SWAP(22, 28); SWAP(23, 29); SWAP(2, 4); SWAP(3, 5); SWAP(6, 8); SWAP(7, 9); SWAP(10, 12); SWAP(11, 13); SWAP(14, 16); SWAP(15, 17); SWAP(18, 20); SWAP(19, 21); SWAP(22, 24); SWAP(23, 25); SWAP(26, 28); SWAP(27, 29); SWAP(2, 3); SWAP(4, 5); SWAP(6, 7); SWAP(8, 9); SWAP(10, 11); SWAP(12, 13); SWAP(14, 15); SWAP(16, 17); SWAP(18, 19); SWAP(20, 21); SWAP(22, 23); SWAP(24, 25); SWAP(26, 27); SWAP(28, 29); SWAP(1, 16); SWAP(3, 18); SWAP(5, 20); SWAP(7, 22); SWAP(9, 24); SWAP(11, 26); SWAP(13, 28); SWAP(15, 30); SWAP(1, 8); SWAP(3, 10); SWAP(5, 12); SWAP(7, 14); SWAP(9, 16); SWAP(11, 18); SWAP(13, 20); SWAP(15, 22); SWAP(17, 24); SWAP(19, 26); SWAP(21, 28); SWAP(23, 30); SWAP(1, 4); SWAP(3, 6); SWAP(5, 8); SWAP(7, 10); SWAP(9, 12); SWAP(11, 14); SWAP(13, 16); SWAP(15, 18); SWAP(17, 20); SWAP(19, 22); SWAP(21, 24); SWAP(23, 26); SWAP(25, 28); SWAP(27, 30); SWAP(1, 2); SWAP(3, 4); SWAP(5, 6); SWAP(7, 8); SWAP(9, 10); SWAP(11, 12); SWAP(13, 14); SWAP(15, 16); SWAP(17, 18); SWAP(19, 20); SWAP(21, 22); SWAP(23, 24); SWAP(25, 26); SWAP(27, 28); SWAP(29, 30); 

//There are 543 comparators in this network, grouped into 21 parallel operations.
#define SORT64 SWAP(0, 32); SWAP(1, 33); SWAP(2, 34); SWAP(3, 35); SWAP(4, 36); SWAP(5, 37); SWAP(6, 38); SWAP(7, 39); SWAP(8, 40); SWAP(9, 41); SWAP(10, 42); SWAP(11, 43); SWAP(12, 44); SWAP(13, 45); SWAP(14, 46); SWAP(15, 47); SWAP(16, 48); SWAP(17, 49); SWAP(18, 50); SWAP(19, 51); SWAP(20, 52); SWAP(21, 53); SWAP(22, 54); SWAP(23, 55); SWAP(24, 56); SWAP(25, 57); SWAP(26, 58); SWAP(27, 59); SWAP(28, 60); SWAP(29, 61); SWAP(30, 62); SWAP(31, 63); SWAP(0, 16); SWAP(1, 17); SWAP(2, 18); SWAP(3, 19); SWAP(4, 20); SWAP(5, 21); SWAP(6, 22); SWAP(7, 23); SWAP(8, 24); SWAP(9, 25); SWAP(10, 26); SWAP(11, 27); SWAP(12, 28); SWAP(13, 29); SWAP(14, 30); SWAP(15, 31); SWAP(32, 48); SWAP(33, 49); SWAP(34, 50); SWAP(35, 51); SWAP(36, 52); SWAP(37, 53); SWAP(38, 54); SWAP(39, 55); SWAP(40, 56); SWAP(41, 57); SWAP(42, 58); SWAP(43, 59); SWAP(44, 60); SWAP(45, 61); SWAP(46, 62); SWAP(47, 63); SWAP(16, 32); SWAP(17, 33); SWAP(18, 34); SWAP(19, 35); SWAP(20, 36); SWAP(21, 37); SWAP(22, 38); SWAP(23, 39); SWAP(24, 40); SWAP(25, 41); SWAP(26, 42); SWAP(27, 43); SWAP(28, 44); SWAP(29, 45); SWAP(30, 46); SWAP(31, 47); SWAP(0, 8); SWAP(1, 9); SWAP(2, 10); SWAP(3, 11); SWAP(4, 12); SWAP(5, 13); SWAP(6, 14); SWAP(7, 15); SWAP(48, 56); SWAP(49, 57); SWAP(50, 58); SWAP(51, 59); SWAP(52, 60); SWAP(53, 61); SWAP(54, 62); SWAP(55, 63); SWAP(16, 24); SWAP(17, 25); SWAP(18, 26); SWAP(19, 27); SWAP(20, 28); SWAP(21, 29); SWAP(22, 30); SWAP(23, 31); SWAP(32, 40); SWAP(33, 41); SWAP(34, 42); SWAP(35, 43); SWAP(36, 44); SWAP(37, 45); SWAP(38, 46); SWAP(39, 47); SWAP(0, 4); SWAP(1, 5); SWAP(2, 6); SWAP(3, 7); SWAP(56, 60); SWAP(57, 61); SWAP(58, 62); SWAP(59, 63); SWAP(8, 32); SWAP(9, 33); SWAP(10, 34); SWAP(11, 35); SWAP(12, 36); SWAP(13, 37); SWAP(14, 38); SWAP(15, 39); SWAP(24, 48); SWAP(25, 49); SWAP(26, 50); SWAP(27, 51); SWAP(28, 52); SWAP(29, 53); SWAP(30, 54); SWAP(31, 55); SWAP(0, 2); SWAP(1, 3); SWAP(60, 62); SWAP(61, 63); SWAP(8, 16); SWAP(9, 17); SWAP(10, 18); SWAP(11, 19); SWAP(12, 20); SWAP(13, 21); SWAP(14, 22); SWAP(15, 23); SWAP(24, 32); SWAP(25, 33); SWAP(26, 34); SWAP(27, 35); SWAP(28, 36); SWAP(29, 37); SWAP(30, 38); SWAP(31, 39); SWAP(40, 48); SWAP(41, 49); SWAP(42, 50); SWAP(43, 51); SWAP(44, 52); SWAP(45, 53); SWAP(46, 54); SWAP(47, 55); SWAP(0, 1); SWAP(62, 63); SWAP(8, 12); SWAP(9, 13); SWAP(10, 14); SWAP(11, 15); SWAP(16, 20); SWAP(17, 21); SWAP(18, 22); SWAP(19, 23); SWAP(24, 28); SWAP(25, 29); SWAP(26, 30); SWAP(27, 31); SWAP(32, 36); SWAP(33, 37); SWAP(34, 38); SWAP(35, 39); SWAP(40, 44); SWAP(41, 45); SWAP(42, 46); SWAP(43, 47); SWAP(48, 52); SWAP(49, 53); SWAP(50, 54); SWAP(51, 55); SWAP(4, 32); SWAP(5, 33); SWAP(6, 34); SWAP(7, 35); SWAP(12, 40); SWAP(13, 41); SWAP(14, 42); SWAP(15, 43); SWAP(20, 48); SWAP(21, 49); SWAP(22, 50); SWAP(23, 51); SWAP(28, 56); SWAP(29, 57); SWAP(30, 58); SWAP(31, 59); SWAP(4, 16); SWAP(5, 17); SWAP(6, 18); SWAP(7, 19); SWAP(12, 24); SWAP(13, 25); SWAP(14, 26); SWAP(15, 27); SWAP(20, 32); SWAP(21, 33); SWAP(22, 34); SWAP(23, 35); SWAP(28, 40); SWAP(29, 41); SWAP(30, 42); SWAP(31, 43); SWAP(36, 48); SWAP(37, 49); SWAP(38, 50); SWAP(39, 51); SWAP(44, 56); SWAP(45, 57); SWAP(46, 58); SWAP(47, 59); SWAP(4, 8); SWAP(5, 9); SWAP(6, 10); SWAP(7, 11); SWAP(12, 16); SWAP(13, 17); SWAP(14, 18); SWAP(15, 19); SWAP(20, 24); SWAP(21, 25); SWAP(22, 26); SWAP(23, 27); SWAP(28, 32); SWAP(29, 33); SWAP(30, 34); SWAP(31, 35); SWAP(36, 40); SWAP(37, 41); SWAP(38, 42); SWAP(39, 43); SWAP(44, 48); SWAP(45, 49); SWAP(46, 50); SWAP(47, 51); SWAP(52, 56); SWAP(53, 57); SWAP(54, 58); SWAP(55, 59); SWAP(4, 6); SWAP(5, 7); SWAP(8, 10); SWAP(9, 11); SWAP(12, 14); SWAP(13, 15); SWAP(16, 18); SWAP(17, 19); SWAP(20, 22); SWAP(21, 23); SWAP(24, 26); SWAP(25, 27); SWAP(28, 30); SWAP(29, 31); SWAP(32, 34); SWAP(33, 35); SWAP(36, 38); SWAP(37, 39); SWAP(40, 42); SWAP(41, 43); SWAP(44, 46); SWAP(45, 47); SWAP(48, 50); SWAP(49, 51); SWAP(52, 54); SWAP(53, 55); SWAP(56, 58); SWAP(57, 59); SWAP(2, 32); SWAP(3, 33); SWAP(6, 36); SWAP(7, 37); SWAP(10, 40); SWAP(11, 41); SWAP(14, 44); SWAP(15, 45); SWAP(18, 48); SWAP(19, 49); SWAP(22, 52); SWAP(23, 53); SWAP(26, 56); SWAP(27, 57); SWAP(30, 60); SWAP(31, 61); SWAP(2, 16); SWAP(3, 17); SWAP(6, 20); SWAP(7, 21); SWAP(10, 24); SWAP(11, 25); SWAP(14, 28); SWAP(15, 29); SWAP(18, 32); SWAP(19, 33); SWAP(22, 36); SWAP(23, 37); SWAP(26, 40); SWAP(27, 41); SWAP(30, 44); SWAP(31, 45); SWAP(34, 48); SWAP(35, 49); SWAP(38, 52); SWAP(39, 53); SWAP(42, 56); SWAP(43, 57); SWAP(46, 60); SWAP(47, 61); SWAP(2, 8); SWAP(3, 9); SWAP(6, 12); SWAP(7, 13); SWAP(10, 16); SWAP(11, 17); SWAP(14, 20); SWAP(15, 21); SWAP(18, 24); SWAP(19, 25); SWAP(22, 28); SWAP(23, 29); SWAP(26, 32); SWAP(27, 33); SWAP(30, 36); SWAP(31, 37); SWAP(34, 40); SWAP(35, 41); SWAP(38, 44); SWAP(39, 45); SWAP(42, 48); SWAP(43, 49); SWAP(46, 52); SWAP(47, 53); SWAP(50, 56); SWAP(51, 57); SWAP(54, 60); SWAP(55, 61); SWAP(2, 4); SWAP(3, 5); SWAP(6, 8); SWAP(7, 9); SWAP(10, 12); SWAP(11, 13); SWAP(14, 16); SWAP(15, 17); SWAP(18, 20); SWAP(19, 21); SWAP(22, 24); SWAP(23, 25); SWAP(26, 28); SWAP(27, 29); SWAP(30, 32); SWAP(31, 33); SWAP(34, 36); SWAP(35, 37); SWAP(38, 40); SWAP(39, 41); SWAP(42, 44); SWAP(43, 45); SWAP(46, 48); SWAP(47, 49); SWAP(50, 52); SWAP(51, 53); SWAP(54, 56); SWAP(55, 57); SWAP(58, 60); SWAP(59, 61); SWAP(2, 3); SWAP(4, 5); SWAP(6, 7); SWAP(8, 9); SWAP(10, 11); SWAP(12, 13); SWAP(14, 15); SWAP(16, 17); SWAP(18, 19); SWAP(20, 21); SWAP(22, 23); SWAP(24, 25); SWAP(26, 27); SWAP(28, 29); SWAP(30, 31); SWAP(32, 33); SWAP(34, 35); SWAP(36, 37); SWAP(38, 39); SWAP(40, 41); SWAP(42, 43); SWAP(44, 45); SWAP(46, 47); SWAP(48, 49); SWAP(50, 51); SWAP(52, 53); SWAP(54, 55); SWAP(56, 57); SWAP(58, 59); SWAP(60, 61); SWAP(1, 32); SWAP(3, 34); SWAP(5, 36); SWAP(7, 38); SWAP(9, 40); SWAP(11, 42); SWAP(13, 44); SWAP(15, 46); SWAP(17, 48); SWAP(19, 50); SWAP(21, 52); SWAP(23, 54); SWAP(25, 56); SWAP(27, 58); SWAP(29, 60); SWAP(31, 62); SWAP(1, 16); SWAP(3, 18); SWAP(5, 20); SWAP(7, 22); SWAP(9, 24); SWAP(11, 26); SWAP(13, 28); SWAP(15, 30); SWAP(17, 32); SWAP(19, 34); SWAP(21, 36); SWAP(23, 38); SWAP(25, 40); SWAP(27, 42); SWAP(29, 44); SWAP(31, 46); SWAP(33, 48); SWAP(35, 50); SWAP(37, 52); SWAP(39, 54); SWAP(41, 56); SWAP(43, 58); SWAP(45, 60); SWAP(47, 62); SWAP(1, 8); SWAP(3, 10); SWAP(5, 12); SWAP(7, 14); SWAP(9, 16); SWAP(11, 18); SWAP(13, 20); SWAP(15, 22); SWAP(17, 24); SWAP(19, 26); SWAP(21, 28); SWAP(23, 30); SWAP(25, 32); SWAP(27, 34); SWAP(29, 36); SWAP(31, 38); SWAP(33, 40); SWAP(35, 42); SWAP(37, 44); SWAP(39, 46); SWAP(41, 48); SWAP(43, 50); SWAP(45, 52); SWAP(47, 54); SWAP(49, 56); SWAP(51, 58); SWAP(53, 60); SWAP(55, 62); SWAP(1, 4); SWAP(3, 6); SWAP(5, 8); SWAP(7, 10); SWAP(9, 12); SWAP(11, 14); SWAP(13, 16); SWAP(15, 18); SWAP(17, 20); SWAP(19, 22); SWAP(21, 24); SWAP(23, 26); SWAP(25, 28); SWAP(27, 30); SWAP(29, 32); SWAP(31, 34); SWAP(33, 36); SWAP(35, 38); SWAP(37, 40); SWAP(39, 42); SWAP(41, 44); SWAP(43, 46); SWAP(45, 48); SWAP(47, 50); SWAP(49, 52); SWAP(51, 54); SWAP(53, 56); SWAP(55, 58); SWAP(57, 60); SWAP(59, 62); SWAP(1, 2); SWAP(3, 4); SWAP(5, 6); SWAP(7, 8); SWAP(9, 10); SWAP(11, 12); SWAP(13, 14); SWAP(15, 16); SWAP(17, 18); SWAP(19, 20); SWAP(21, 22); SWAP(23, 24); SWAP(25, 26); SWAP(27, 28); SWAP(29, 30); SWAP(31, 32); SWAP(33, 34); SWAP(35, 36); SWAP(37, 38); SWAP(39, 40); SWAP(41, 42); SWAP(43, 44); SWAP(45, 46); SWAP(47, 48); SWAP(49, 50); SWAP(51, 52); SWAP(53, 54); SWAP(55, 56); SWAP(57, 58); SWAP(59, 60); SWAP(61, 62); 

void Sort()
{
	#if (SORT_SIZE == 8)
	SORT8
	#endif	
	#if (SORT_SIZE == 16)
	SORT16
	#endif
	#if (SORT_SIZE == 32)
	SORT32
	#endif
	#if (SORT_SIZE == 64)
	SORT64
	#endif	
}

float medians[SORT_SIZE];

float quant(float x)
{
	x = clamp(x,0.,1.);
	return floor(x*255.+0.5);
}

float pack(vec3 c)
{	
	float lum = (c.x+c.y+c.z)*(1./3.);

#if COLOR	
	//want to sort by luminance I guess so put that in MSB and quantize everything to 8 bit
	//since floats represent 24 bit ints you get 3 channels and only have to sort a scalar value
	return quant(c.x) + quant(c.y)*256. + quant(lum) * 65536.;
#else	
	return lum;
#endif	
}

vec3 unpack(float x)
{
#if COLOR		
	float lum = floor(x * (1./65536.)) * (1./255.);
	vec3 c;
	c.x = floor(mod(x,256.)) 			* (1./255.);
	c.y = floor(mod(x*(1./256.),256.)) * (1./255.);
	c.z = lum * 3. - c.y - c.x;
	return c;
#else
	return vec3(x);	
#endif	
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 ooRes = vec2(1) / iResolution.xy;

#if RAMP	
	//pick a radius to ramp up and down to demo the effect ... sorting networks are fixed size.
	float r = float(radius)*iRenderScale.x;
#endif
	
	//do a bunch of 1D sorts on X
	for (int j=0; j<SORT_SIZE; j++)
	{
		//gather all X the texels for this Y
		for (int i=0; i<SORT_SIZE; i++)
		{
			vec2 uv = (fragCoord.xy + vec2(i,j)-vec2(SORT_SIZE/2)) * ooRes;
			//uv.y=1.-uv.y; //upside down
			float c = pack( texture2D(iChannel0,uv).xyz );
															
#if RAMP	
			if (float(i)<float(SORT_SIZE/2) - r) c=-1e10;	//force to beginning of sorted list
			if (float(i)>float(SORT_SIZE/2) + r) c=1e10;	//force to end of sorted list
#endif			
			sort[i]=c;			
		}
			
		Sort();
		
		//keep the median from X
		float m = sort[(SORT_SIZE/2)];

#if RAMP			
		if (float(j)<float(SORT_SIZE/2) - r) m=-1e10;
		if (float(j)>float(SORT_SIZE/2) + r) m=1e10;				
#endif	
		medians[j] = m;
	}

	//sort the medians
	for (int i=0; i<SORT_SIZE; i++)
	{
		sort[i]=medians[i];
	}	
	Sort();
	
	//median of medians is pretty near the true median
	fragColor = vec4(unpack(sort[(SORT_SIZE/2)]),1.0);
}
