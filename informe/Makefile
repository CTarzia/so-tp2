

.PHONY: clean informe.pdf

informe.pdf : informe.tex
	pdflatex $^
	bibtex informe || echo "no citations"
	pdflatex $^
	pdflatex $^

clean:
	rm -f informe.toc informe.aux informe.log informe.out informe.bbl informe.blg

