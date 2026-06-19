
document.getElementById('form').onsubmit=async(e)=>{
e.preventDefault();
let fd=new FormData(e.target);
let r=await fetch('/generate',{method:'POST',body:fd});
let d=await r.json();
document.getElementById('result').innerHTML=`
<a href="${d.photo}">Download Photo</a><br>
<a href="${d.a4}">Download A4 Sheet</a>`;
};
