//---------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "sndfile.h"

//---------------------------------------------------------------------------
#pragma pack(push,1)
typedef struct
{
	char mark[6];  // = "TCWF0\x1a"
	uint8_t channels; // チャネル数
	uint8_t reserved;
	int32_t frequency; // サンプリング周波数
	int32_t numblocks; // ブロック数
	int32_t bytesperblock; // ブロックごとのバイト数
	int32_t samplesperblock; // ブロックごとのサンプル数
} TTCWFHeader;

typedef struct
{
	uint16_t pos;
	int16_t revise;
} TTCWUnexpectedPeak;

typedef struct  // ブロックヘッダ ( ステレオの場合はブロックが右・左の順に２つ続く)
{
	int16_t ms_sample0;
	int16_t ms_sample1;
	int16_t ms_idelta;
	uint8_t ms_bpred;
	uint8_t ima_stepindex;
	TTCWUnexpectedPeak peaks[6];
} TTCWBlockHeader;
#pragma pack(pop)
//---------------------------------------------------------------------------
static
uint32_t srate2blocksize (uint32_t srate)
{	if (srate < 12000)
		return 256 ;
	if (srate < 23000)
		return 512 ;
	return 1024 ;
} /* srate2blocksize */
//---------------------------------------------------------------------------

/*============================================================================================
** MS ADPCM static data.
*/
static int AdaptationTable []    =
{	230, 230, 230, 230, 307, 409, 512, 614,
	768, 614, 512, 409, 307, 230, 230, 230
} ;


/* TODO : The first 7 coef's are are always hardcode and must
   appear in the actual WAVE file.  They should be read in
   in case a sound program added extras to the list. */

static int AdaptCoeff1 [] =
{	256, 512, 0, 192, 240, 460, 392
} ;

static int AdaptCoeff2 [] =
{	0, -256, 0, 64, 0, -208, -232
} ;

/*============================================================================================
** Predefined IMA ADPCM data.
*/
static int ima_index_adjust [16] =
{	-1, -1, -1, -1,		// +0 - +3, decrease the step size
	 2,  4,  6,  8,     // +4 - +7, increase the step size
	-1, -1, -1, -1,		// -0 - -3, decrease the step size
	 2,  4,  6,  8,		// -4 - -7, increase the step size
} ;


static int ima_step_size [89] = 
{	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
	11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 
	32767
} ;

bool decode_block(TTCWFHeader *header, int16_t *samples, int numsamples, FILE *fin, uint8_t* blockbuffer, int chan)
{

	// メモリ確保
	// if(!BlockBuffer)
	// 	BlockBuffer=new uint8_t[Header.bytesperblock];
	// if(!this->Samples)
	// 	this->Samples=new int16_t[Header.samplesperblock * Header.channels];

	int16_t * Samples = samples + chan;

	int numchans = header->channels;

	// ブロックヘッダ読み込み
	TTCWBlockHeader bheader;
	size_t read1 = fread(&bheader, 1, sizeof(bheader), fin);
	if(sizeof(bheader)!=read1) return false;
	size_t read2 = fread(blockbuffer, 1, header->bytesperblock - sizeof(bheader), fin);
	if(header->bytesperblock- sizeof(bheader)!=read2) return false;

	// デコード
	Samples[0*numchans] = bheader.ms_sample0;
	Samples[1*numchans] = bheader.ms_sample1;
	int idelta = bheader.ms_idelta;
	int bpred = bheader.ms_bpred;
	if(bpred>=7) return false; // おそらく同期がとれていない

	int k;
	int p;

	//MS ADPCM デコード
	int predict;
	int bytecode;
	for (k = 2, p = 0 ; k < header->samplesperblock ; k ++, p++)
	{
		bytecode = blockbuffer[p] & 0xF ;

	    int idelta_save=idelta;
		idelta = (AdaptationTable [bytecode] * idelta) >> 8 ;
	    if (idelta < 16) idelta = 16;
	    if (bytecode & 0x8) bytecode -= 0x10 ;
	
    	predict = ((Samples [(k - 1)*numchans] * AdaptCoeff1 [bpred]) 
					+ (Samples [(k - 2)*numchans] * AdaptCoeff2 [bpred])) >> 8 ;
 
		int current = (bytecode * idelta_save) + predict;
    
	    if (current > 32767) 
			current = 32767 ;
	    else if (current < -32768) 
			current = -32768 ;
    
		Samples [k*numchans] = (int16_t) current ;
	};

	//IMA ADPCM デコード
	int step;
	int stepindex = bheader.ima_stepindex;
	int prev = 0;
	int diff;
	for (k = 2, p = 0 ; k < header->samplesperblock ; k ++, p++)
	{
		bytecode= (blockbuffer[p]>>4) & 0xF;
		
		step = ima_step_size [stepindex] ;
		int current = prev;
  

		diff = step >> 3 ;
		if (bytecode & 1) 
			diff += step >> 2 ;
		if (bytecode & 2) 
			diff += step >> 1 ;
		if (bytecode & 4) 
			diff += step ;
		if (bytecode & 8) 
			diff = -diff ;

		current += diff ;

		if (current > 32767) current = 32767;
		else if (current < -32768) current = -32768 ;

		stepindex+= ima_index_adjust [bytecode] ;
	
		if (stepindex< 0)  stepindex = 0 ;
		else if (stepindex > 88)	stepindex = 88 ;

		prev = current ;

		int n = Samples[k*numchans];
		n+=current;
		if (n > 32767) n = 32767;
		else if (n < -32768) n = -32768 ;
		Samples[k*numchans] =n;
	};

	// unexpected peak の修正
	int i;
	for(i=0; i<6; i++)
	{
		if(bheader.peaks[i].revise)
		{
			int pos = bheader.peaks[i].pos;
			int n = Samples[pos*numchans];
			n -= bheader.peaks[i].revise;
			if (n > 32767) n = 32767;
			else if (n < -32768) n = -32768 ;
			Samples[pos*numchans] = n;
		}
	}

	return true;
}
//---------------------------------------------------------------------------
void decode(const char *srcfilename, const char *destfilename)
{
	SNDFILE *sfout;
	FILE *fin;
	SF_INFO sfiout;
	size_t read;
	int i;

	fin = fopen(srcfilename, "rb");
	if(!fin)
	{
		printf("Can't open : %s\n", srcfilename);
		return;
	}

	int chans;
	TTCWFHeader header;

	read = fread(&header, 1, sizeof(header), fin);
	if(read!=sizeof(header))
	{
		printf("Could not read header\n");
		return;
	}
	if(memcmp(header.mark,"TCWF0\x1a", 6))
	{
		printf("Header does not match\n");
		return;
	}

	memset(&sfiout, 0, sizeof(sfiout));
	sfiout.samplerate = header.frequency;
	sfiout.channels = header.channels;
	chans = sfiout.channels;
	sfiout.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
	sfout = sf_open(destfilename, SFM_WRITE, &sfiout);
	if(!sfout)
	{
		printf("Can't open %s: %s\n", destfilename, sf_strerror(sfout));
		return;
	}

	printf("Decompressing %s -> %s ...\n", srcfilename , destfilename);

	uint8_t* blockbuffer = (uint8_t *)malloc(header.bytesperblock);
	if (!blockbuffer)
	{
		printf("Could not allocate block buffer");
		return;
	}

	int16_t* samples = (int16_t*)malloc(header.samplesperblock * sizeof(int16_t) * header.channels);
	if (!samples)
	{
		printf("Could not allocate sample buffer");
		return;
	}

	while( true )
	{
		for(i=0; i<header.channels; i++)
		{
			if(!decode_block(&header, samples, header.samplesperblock, fin, blockbuffer, i))
			{
				goto done;
			}
		}
		sf_writef_short(sfout, samples, header.samplesperblock);
	}

done:
	free(blockbuffer);
	free(samples);
	fclose(fin);
	sf_close(sfout);

	return;
}


/*----------------------------------------------------------------------------------------
**	Choosing the block predictor.
**	Each block requires a predictor and an idelta for each channel. 
**	The predictor is in the range [0..6] which is an index into the	two AdaptCoeff tables. 
**	The predictor is chosen by trying all of the possible predictors on a small set of
**	samples at the beginning of the block. The predictor with the smallest average
**	abs (idelta) is chosen as the best predictor for this block. 
**	The value of idelta is chosen to to give a 4 bit code value of +/- 4 (approx. half the 
**	max. code value). If the average abs (idelta) is zero, the sixth predictor is chosen.
**	If the value of idelta is less then 16 it is set to 16.
**
**	Microsoft uses an IDELTA_COUNT (number of sample pairs used to choose best predictor)
**	value of 3. The best possible results would be obtained by using all the samples to
**	choose the predictor.
*/
static
void	choose_predictor (int IDELTA_COUNT, int16_t *data,
	int *block_pred, int *idelta)
{	uint32_t	k, bpred, idelta_sum, best_bpred, best_idelta ;

	best_bpred = best_idelta = 0 ;

	for (bpred = 0 ; bpred < 7 ; bpred++)
	{
		idelta_sum = 0 ;
		for (k = 2 ; k < 2 + IDELTA_COUNT ; k++)
			idelta_sum += abs (data [k] - ((data [k-1] * AdaptCoeff1 [bpred] + data [k-2] * AdaptCoeff2 [bpred]) >> 8)) ;
		idelta_sum /= (4 * IDELTA_COUNT) ;

		if (bpred == 0 || idelta_sum < best_idelta)
		{	best_bpred = bpred ;
			best_idelta = idelta_sum ;
			} ;

		if (! idelta_sum)
		{	best_bpred = bpred ;
			best_idelta = 16 ;
			break ;
			} ;
			} ; /* for bpred ... */

	if (best_idelta < 16)
		best_idelta = 16 ;

	*block_pred = best_bpred ;
	*idelta     = best_idelta ;

	return ;
} /* choose_predictor */

//---------------------------------------------------------------------------
void encode_block(TTCWFHeader *header, int16_t *samples, int numsamples, FILE *fout)
{
	TTCWBlockHeader bheader;
	int16_t temp[32768];
	uint8_t out[32768];

	int bpred=0;
	int idelta=0;
	int	k, predict, errordelta, newsamp, ms_diff;
	uint8_t ms_nibble;
	uint8_t ima_nibble;
	static int stepindex = 0; /* static !! */
	int prev = 0;
	int diff;
	int step;
	int vpdiff;
	int mask;

	choose_predictor (numsamples-2, samples,  &bpred, &idelta);

	bheader.ms_bpred = bpred;
	bheader.ms_idelta = idelta;
	bheader.ms_sample0 = samples[0];
	bheader.ms_sample1 = samples[1];
	bheader.ima_stepindex = stepindex;

	temp[0]=0;
	temp[1]=0;

	int p=0;
	for (k = 2 ; k < numsamples ; k++)
	{
		/* MS ADPCM 圧縮部 */

		predict = (samples[k-1] * AdaptCoeff1 [bpred] + samples [k-2] * AdaptCoeff2 [bpred]) >> 8 ;
		errordelta = (samples [k] - predict) / idelta;

		if (errordelta < -8)
			errordelta = -8 ;
		else if (errordelta > 7)
			errordelta = 7 ;

		newsamp = predict + (idelta* errordelta) ;

		if (newsamp > 32767)
			newsamp = 32767 ;
		else if (newsamp < -32768)
			newsamp = -32768 ;
		if (errordelta < 0)
			errordelta += 0x10 ;

		ms_nibble = (errordelta & 0xF);

		idelta= (idelta * AdaptationTable [errordelta]) >> 8 ;

		if (idelta< 16)
			idelta= 16 ;

		ms_diff = samples[k] - newsamp;


		int16_t orgsamp = samples[k];
		samples [k] = newsamp ;

		/* IMA ADPCM 圧縮部 */

		diff = ms_diff-prev ;


		ima_nibble = 0 ;
		step = ima_step_size[stepindex] ;
		vpdiff = step >> 3 ;
		if (diff < 0)
		{
			ima_nibble = 8 ;
			diff = -diff ;
		}
		mask = 4 ;

		while (mask)
		{
			if (diff >= step)
			{
				ima_nibble |= mask ;
				diff -= step ;
				vpdiff += step ;
			}
			step >>= 1 ;
			mask >>= 1 ;
		}

		if (ima_nibble & 8)
			prev -= vpdiff ;
		else
			prev += vpdiff ;

		if (prev > 32767) prev = 32767;
		else if (prev < -32768) prev = -32768;

		temp[k] = orgsamp - (newsamp + prev);

		stepindex += ima_index_adjust [ima_nibble] ;
		if (stepindex< 0) stepindex = 0 ;
		else if (stepindex > 88) stepindex= 88 ;

		ima_nibble&=0x0f;

		out[p++] = (uint8_t)((ms_nibble<<0) + (ima_nibble<<4));

	}

	/* Unexpected Peak の検出と記録 */

	for(int i=0; i<6; i++)
	{
		int max =0;
		int max_pos = 0;
		int k;

		for(k=2; k<numsamples; k++)
		{
			if(temp[k]<0)
			{
				if(-temp[k]>max)
				{
					max=-temp[k];
					max_pos=k;
				}
			}
			else
			{
				if(temp[k]>max)
				{
					max=temp[k];
					max_pos=k;
				}
			}
		}
		bheader.peaks[i].pos = max_pos;
		bheader.peaks[i].revise = -temp[max_pos];
		temp[max_pos] += bheader.peaks[i].revise; // 0 になる
	}


	// ファイルに出力
	fwrite(&bheader, sizeof(bheader), 1, fout);
	fwrite(out, p, 1, fout);
}
//---------------------------------------------------------------------------
void encode(const char *srcfilename, const char*destfilename)
{
	int16_t buf[32768];
	SNDFILE *sfin;
	FILE *fout;
	SF_INFO sfiin;
	size_t read;
	int i;


	// ソースファイルを開く
	memset(&sfiin, 0, sizeof(sfiin));
	sfin = sf_open(srcfilename, SFM_READ, &sfiin);
	if(!sfin)
	{
		printf("Can't open : %s\n", srcfilename);
		return;
	}

	// 出力ファイルを開く
	fout = fopen(destfilename, "wb");
	if(!fout)
	{
		printf("Can't open : %s\n", destfilename);
	}

	// 表示
	printf("Compressing %s -> %s ...\n", srcfilename , destfilename);


	// ヘッダを準備
	int chans;
	TTCWFHeader header;
	memcpy(header.mark, "TCWF0\x1a", 6);
	chans = header.channels = sfiin.channels;
	header.reserved = 0;
	header.frequency = sfiin.samplerate;
	header.numblocks = 0; // ここは後でうめる
	header.bytesperblock = srate2blocksize(header.frequency);
	header.samplesperblock = header.bytesperblock - sizeof(TTCWBlockHeader) + 2;

	fwrite(&header, sizeof(header), 1, fout); // とりあえずいったん書いておく

	int numblocks = 0;
	int totalblocks = sfiin.frames*chans / header.samplesperblock;

	while( 0 < ( read = sf_read_short(sfin, buf, header.samplesperblock*chans) )   )
	{
		// ステレオの場合はインターリーブ解除
		int16_t buf2[2][32768];
		memset(buf2[0], 0, sizeof(int16_t)*32768);
		memset(buf2[1], 0, sizeof(int16_t)*32768);

		if(chans==1)
		{
			memcpy(buf2[0], buf, sizeof(int16_t)*read);
		}
		else
		{
			for(i=0; i<read; i+=2)
			{
				buf2[0][i/2]=buf[i];
				buf2[1][i/2]=buf[i+1];
			}
		}

		if(chans==1)
		{
			encode_block(&header, buf2[0], header.samplesperblock, fout);
			numblocks ++;
		}
		else
		{
			encode_block(&header, buf2[0], header.samplesperblock, fout);
			encode_block(&header, buf2[1], header.samplesperblock, fout);
			numblocks += 2;
		}
	}

	header.numblocks = numblocks;
	fseek(fout, 0, SEEK_SET);
	fwrite(&header, sizeof(header), 1, fout); // ヘッダをもう一回書き込む

	sf_close(sfin);
	fclose(fout);

	return;
}
//---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
	if(argc<=1)
	{
		printf("TCWF utility  copyright (C) 2020 Julian Uy\n");
		printf("  https://github.com/uyjulian/tcwfutil\n");
		printf("Original TCWF compressor  copyright (C) 2000 W.Dee\n");
		printf("\n");
		printf("Usage : tcwfutil <filename>\n");
		printf("         Under Windows GUI, you can drag source file(s) that you want\n");
		printf("        to compress or decompress, then drop on this application.\n");
		printf("\n");
		printf("  <filename> file can be one of following format :\n");
		printf("  .WAV .AIFF .AU .PAF .SVX .TCW\n");
		printf("\n");
		printf("  Output filename will be automatically the same as input filename,\n"
			   " except for the appended extension that has \".tcw\" and/or \".wav\".\n");
		printf("\n");
		printf("  You can use/modify/redistribute this program under GNU GPL, see\n"
			   " the document for details.\n");
		return 0;
	}

	int i;
	for(i=1; i<argc; i++)
	{
		bool is_tcwf = false;
		FILE *fin = fopen(argv[i], "rb");
		if(!fin)
		{
			printf("Can't open : %s\n", argv[i]);
			continue;
		}

		char magic[6];
		int read = fread(&magic, 1, sizeof(magic), fin);
		fclose(fin);
		if (read == sizeof(magic))
		{
			if (!memcmp(magic,"TCWF0\x1a", 6))
			{
				is_tcwf = true;
			}
		}

		int fn_len = strlen(argv[i]);
		char *fn_with_ext = (char *)malloc(fn_len + 5);
		if (!fn_with_ext)
		{
			printf("Can't allocate memory for filename\n");
			return 1;
		}
		strcpy(fn_with_ext, argv[i]);
		fn_with_ext[fn_len + 4] = '\0';
		if (is_tcwf)
		{
			memcpy(fn_with_ext + fn_len, ".wav", 4);
			decode(argv[i], fn_with_ext);
		}
		else
		{
			memcpy(fn_with_ext + fn_len, ".tcw", 4);
			encode(argv[i], fn_with_ext);
		}
		free(fn_with_ext);
	}
	return 0;
}
//---------------------------------------------------------------------------




