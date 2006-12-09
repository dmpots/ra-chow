#Make file for register allocation paper

PAPER=chowVSchatin
PAPER_TEX=$(PAPER).tex
PAPER_AUX=$(PAPER).aux
PAPER_BIB=ra-chow.bib
PAPER_DVI=$(PAPER).dvi
PAPER_PDF=$(PAPER).pdf

default: .pdf

.dvi: $(PAPER_DVI) 
.pdf: $(PAPER_DVI)
	@dvipdf $(PAPER_DVI)
	@echo "-- make $(PAPER_PDF) (done)"
  
$(PAPER_DVI): $(PAPER_TEX)
	@latex $(PAPER_TEX)
	@echo "-- make $@ (done)"

ref: 
	@latex $(PAPER_TEX)
	@bibtex $(PAPER)
	@latex $(PAPER_TEX)
	@latex $(PAPER_TEX)
	@echo "-- make $@ (done)"
	
clean:
	@rm -f *.aux
	@rm -f *.bbl
	@rm -f *.blg
	@rm -f *.dvi
	@rm -f *.log
	@rm -f $(PAPER_PDF)
	@echo "-- make $@ (done)"




